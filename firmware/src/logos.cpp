#include "logos.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_partition.h>
#include <lgfx/utility/lgfx_pngle.h>
#include <rom/crc.h>
#include <vector>
#include "app.h"

// Storage: the data partition is used RAW as 32 fixed slots of 28 KB (no filesystem).
// LittleFS was dropped because a stray close() of a recycled descriptor inside the ESP32
// network stack kept closing our open logo files mid-read (LittleFS assert -> reboot).
// Raw partition reads use no file descriptors, so nothing can pull them out from under us.
//
// Slot layout: [header 16 B][w*h ARGB8888 pixels]. The header is written LAST, so a slot
// interrupted by a power cut simply reads as empty.
//
// Threading: the network task downloads + decodes into RAM; the UI task writes slots in
// logoFlush() and reads them in logoDraw(). Shared lists are guarded by qMutex.

static constexpr uint32_t MAGIC = 0x314F474Cu;  // "LGO1"
static constexpr size_t SLOT = 28 * 1024;
static constexpr int MAXPX = 80;  // largest logo we draw (score alert)

struct Hdr {
  uint32_t magic, crc;  // crc32 of the logo path
  uint16_t size;        // requested size (key), w/h may differ slightly
  uint8_t w, h;
  uint32_t seq;
};
static_assert(sizeof(Hdr) == 16, "slot header");

struct Slot {
  bool used;
  uint32_t crc;
  uint16_t size;
  uint8_t w, h;
  uint32_t lastUse;  // millis, for eviction
};

struct Req {
  String path;
  int size;
  int tries;
};
struct Ready {  // decoded logo waiting for the UI to store (px == nullptr: "ESPN doesn't have it")
  uint32_t crc;
  int size;
  lgfx::argb8888_t* px;
  int w, h;
};
struct Key {
  uint32_t crc;
  uint16_t size, pad;
};

static const esp_partition_t* part = nullptr;
static std::vector<Slot> slots;
static uint32_t nextSeq = 1;
static SemaphoreHandle_t qMutex;
static std::vector<Req> queue;
static std::vector<Ready> ready;
static std::vector<Key> failed;  // logos ESPN doesn't have (persisted in NVS)

struct QLock {
  QLock() { xSemaphoreTake(qMutex, portMAX_DELAY); }
  ~QLock() { xSemaphoreGive(qMutex); }
};

static uint32_t crcOf(const String& path) { return crc32_le(0, (const uint8_t*)path.c_str(), path.length()); }

static int findSlot(uint32_t crc, int size) {
  for (size_t i = 0; i < slots.size(); i++)
    if (slots[i].used && slots[i].crc == crc && slots[i].size == size) return i;
  return -1;
}
static bool isFailed(uint32_t crc, int size) {
  for (auto& k : failed)
    if (k.crc == crc && k.size == size) return true;
  return false;
}

void logosBegin() {
  qMutex = xSemaphoreCreateMutex();
  part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!part) return;
  int n = min<int>(32, part->size / SLOT);
  slots.assign(n, Slot{false, 0, 0, 0, 0, 0});
  for (int i = 0; i < n; i++) {
    Hdr h;
    if (esp_partition_read(part, i * SLOT, &h, sizeof h) != ESP_OK) continue;
    if (h.magic != MAGIC || !h.w || !h.h || h.w > MAXPX || h.h > MAXPX) continue;
    slots[i] = Slot{true, h.crc, h.size, h.w, h.h, 0};
    nextSeq = max(nextSeq, h.seq + 1);
  }
  Preferences p;
  p.begin("logos", false);  // read-write so a missing namespace is created instead of logging an error
  size_t len = p.getBytesLength("fail");
  if (len && len % sizeof(Key) == 0 && len <= 64 * sizeof(Key)) {
    failed.resize(len / sizeof(Key));
    p.getBytes("fail", failed.data(), len);
  }
  p.end();
}

// ESPN logo paths come in light ("/500/") and dark-background ("/500-dark/") variants.
String themedLogoPath(const char* path, bool dark) {
  String p = path ? path : "";
  int host = p.indexOf("espncdn.com");  // accept full URLs (saved config) as well as bare paths
  if (host >= 0) p = p.substring(host + strlen("espncdn.com"));
  if (dark) p.replace("/500/", "/500-dark/");
  else p.replace("/500-dark/", "/500/");
  return p;
}

// Some teams have no dark-background variant: fall back to the regular logo.
// Returns the slot index, or -1 if there's nothing to draw yet (and queues the download).
static int lookup(const char* rawPath, int size, bool dark) {
  if (!part || !rawPath || !*rawPath || size > MAXPX) return -1;
  String path = themedLogoPath(rawPath, dark);
  QLock l;
  uint32_t crc = crcOf(path);
  if (dark && isFailed(crc, size)) {
    path = themedLogoPath(rawPath, false);
    crc = crcOf(path);
  }
  int s = findSlot(crc, size);
  if (s >= 0) return s;
  if (isFailed(crc, size)) return -1;
  bool queued = queue.size() >= 40;
  for (auto& r : queue)
    if (r.path == path && r.size == size) queued = true;
  for (auto& r : ready)
    if (r.crc == crc && r.size == size) queued = true;
  if (!queued) queue.push_back({path, size, 0});
  return -1;
}

void logoPrefetch(const char* rawPath, int size, bool dark) { lookup(rawPath, size, dark); }

int logoPending() {
  QLock l;
  return queue.size() + ready.size();
}

// ------------------------------------------------------------------ UI side
bool logoDraw(LovyanGFX& dst, const char* rawPath, int size, bool dark, int x, int y, int clipTop, int clipBottom) {
  int s = lookup(rawPath, size, dark);
  if (s < 0) return false;
  Slot sl = slots[s];
  sl.lastUse = millis();
  slots[s].lastUse = sl.lastUse;
  int w = sl.w, h = sl.h;
  lgfx::argb8888_t row[MAXPX];
  int r0 = max(0, clipTop - y), r1 = min(h, clipBottom - y);
  for (int r = r0; r < r1; r++) {
    if (esp_partition_read(part, s * SLOT + sizeof(Hdr) + (size_t)r * w * 4, row, w * 4) != ESP_OK) return false;
    dst.pushAlphaImage(x, y + r, w, 1, row);
  }
  return true;
}

static void saveFailed() {
  Preferences p;
  p.begin("logos", false);
  QLock l;
  p.putBytes("fail", failed.data(), failed.size() * sizeof(Key));
  p.end();
}

// Stores logos the network task finished. Call often from the UI task; true if any landed.
bool logoFlush() {
  bool any = false;
  for (;;) {
    Ready r;
    {
      QLock l;
      if (ready.empty()) break;
      r = ready.front();
      ready.erase(ready.begin());
    }
    if (!r.px) {
      saveFailed();
      continue;
    }
    // pick a free slot, else the one used longest ago
    int pick = 0;
    for (size_t i = 0; i < slots.size(); i++) {
      if (!slots[i].used) {
        pick = i;
        break;
      }
      if (slots[i].lastUse < slots[pick].lastUse) pick = i;
    }
    size_t off = pick * SLOT, n = (size_t)r.w * r.h * 4;
    {
      QLock l;
      slots[pick].used = false;  // invalid while being rewritten
    }
    Hdr h{MAGIC, r.crc, (uint16_t)r.size, (uint8_t)r.w, (uint8_t)r.h, nextSeq++};
    bool ok = esp_partition_erase_range(part, off, SLOT) == ESP_OK &&
              esp_partition_write(part, off + sizeof(Hdr), r.px, n) == ESP_OK &&
              esp_partition_write(part, off, &h, sizeof h) == ESP_OK;
    free(r.px);
    if (ok) {
      QLock l;
      slots[pick] = Slot{true, r.crc, (uint16_t)r.size, (uint8_t)r.w, (uint8_t)r.h, millis()};
      any = true;
    } else {
      Serial.printf("logo slot %d: write failed\n", pick);
    }
  }
  return any;
}

// ------------------------------------------------------------------ network side
struct MemReader {
  const uint8_t* p;
  size_t n, pos;
};
static uint32_t memRead(void* u, uint8_t* buf, uint32_t len) {
  auto* m = (MemReader*)u;
  uint32_t k = min<uint32_t>(len, m->n - m->pos);
  if (buf) memcpy(buf, m->p + m->pos, k);  // buf == NULL means "skip" (pngle skips CRCs this way)
  m->pos += k;
  return k;
}
struct Canvas {
  lgfx::argb8888_t* px;
  int w, h;
};
static Canvas* curCanvas = nullptr;
static void pngDraw(void*, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t len, const uint8_t* argb) {
  Canvas* c = curCanvas;
  if (!c || (int)y >= c->h) return;
  // pngle packs each pixel as R<<8 | G<<16 | B<<24 | A, which is NOT argb8888_t's layout
  // (reading it raw turned transparent white pixels into opaque yellow).
  auto* src = (const uint32_t*)argb;
  for (size_t i = 0; i < len; i++) {
    uint32_t xx = x + i * div_x;
    if ((int)xx >= c->w) continue;
    uint32_t p = src[i];
    lgfx::argb8888_t& o = c->px[y * c->w + xx];
    o.a = p & 0xFF;
    o.r = (p >> 8) & 0xFF;
    o.g = (p >> 16) & 0xFF;
    o.b = (p >> 24) & 0xFF;
  }
}

// 1 = ok (out filled), 0 = try again later, -1 = gone (4xx / undecodable)
static int download(const Req& r, Canvas& out) {
  std::vector<uint8_t> png;
  {
    // ESPN's resizer returns a PNG (with transparency) at exactly the size we draw.
    String url = "https://a.espncdn.com/combiner/i?img=" + r.path + "&w=" + r.size + "&h=" + r.size;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    if (!http.begin(client, url)) return 0;
    int code = http.GET();
    if (code != 200) {
      http.end();
      Serial.printf("logo %s @%d: HTTP %d\n", r.path.c_str(), r.size, code);
      return (code >= 400 && code < 500) ? -1 : 0;
    }
    int len = http.getSize();
    if (len > 48 * 1024) {
      http.end();
      return -1;
    }
    png.reserve(len > 0 ? len : 8192);
    WiFiClient* s = http.getStreamPtr();
    uint8_t buf[512];
    uint32_t t0 = millis();
    while (http.connected() && (len < 0 || (int)png.size() < len) && millis() - t0 < 10000) {
      int n = s->available() ? s->read(buf, sizeof buf) : 0;
      if (n > 0) {
        png.insert(png.end(), buf, buf + n);
        if (png.size() > 48 * 1024) break;
      } else {
        delay(5);
      }
    }
    http.end();
    if (png.size() < 8 || (len > 0 && (int)png.size() != len)) return 0;
  }  // TLS connection fully released here, before we decode

  pngle_t* pngle = lgfx_pngle_new();
  if (!pngle) return 0;
  MemReader mr{png.data(), png.size(), 0};
  if (lgfx_pngle_prepare(pngle, memRead, &mr) < 0) {
    lgfx_pngle_destroy(pngle);
    return -1;
  }
  int w = lgfx_pngle_get_width(pngle), h = lgfx_pngle_get_height(pngle);
  if (w <= 0 || h <= 0 || w > MAXPX || h > MAXPX) {
    lgfx_pngle_destroy(pngle);
    return -1;
  }
  out = {(lgfx::argb8888_t*)calloc(w * h, 4), w, h};
  if (!out.px) {
    lgfx_pngle_destroy(pngle);
    return 0;
  }
  curCanvas = &out;
  int res = lgfx_pngle_decomp(pngle, pngDraw);
  curCanvas = nullptr;
  lgfx_pngle_destroy(pngle);
  if (res < 0) {
    free(out.px);
    out.px = nullptr;
    return -1;
  }
  return 1;
}

void logoService() {
  for (int i = 0; i < 3; i++) {
    Req r;
    {
      QLock l;
      // don't pile decoded pixels up in RAM faster than the UI stores them
      if (queue.empty() || ready.size() >= 2) return;
      r = queue.front();
    }
    Canvas c{nullptr, 0, 0};
    int res = download(r, c);
    uint32_t crc = crcOf(r.path);
    {
      QLock l;
      if (!queue.empty()) queue.erase(queue.begin());
      if (res > 0) ready.push_back({crc, r.size, c.px, c.w, c.h});
      if (res < 0) {
        if (failed.size() < 64) failed.push_back({crc, (uint16_t)r.size, 0});
        ready.push_back({crc, r.size, nullptr, 0, 0});  // UI persists the failed list
      }
      // a transient failure goes to the back of the line so it can't hold up the other logos;
      // after 3 tries it's dropped (drawing or the next prefetch queues it again)
      if (res == 0 && ++r.tries < 3) queue.push_back(r);
    }
    if (res == 0 && WiFi.status() != WL_CONNECTED) return;
  }
}
