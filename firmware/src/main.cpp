// Sports Ticker — live scores for your favorite teams on an ESP32-2432S028.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include "app.h"
#include "config.h"
#include "demo.h"
#include "espn.h"
#include "gfx.h"
#include "lgfx_cyd.h"
#include "logos.h"
#include "screens.h"
#include "web.h"

static const char* AP_NAME = "SportsTicker-Setup";
static const char* HOSTNAME = "sportsticker";
static const int PIN_R = 4, PIN_G = 16, PIN_B = 17;  // on-board RGB LED, active LOW

LGFX lcd;
Gfx gfx(lcd);
volatile bool dataDirty = true;
volatile bool demoRequest = false, demoExitRequest = false;

struct ScoreEvent {
  int idx, delta;
  bool opponent;
  ScoreEvent(int i = -1, int d = 0, bool o = false) : idx(i), delta(d), opponent(o) {}
};
static ScoreEvent pendingScore;  // written by the fetch task under dataMutex
static volatile uint32_t lastFetchOk = 0;
static TaskHandle_t fetchHandle = nullptr;
static volatile bool refreshRequest = false;  // UI -> fetch task: refetch everything now
static volatile bool refreshDone = false;     // fetch task -> UI: that round finished
static uint32_t refreshStarted = 0;

// ----------------------------------------------------------------- LED
static void led(bool r, bool g, bool b) {
  digitalWrite(PIN_R, r ? LOW : HIGH);
  digitalWrite(PIN_G, g ? LOW : HIGH);
  digitalWrite(PIN_B, b ? LOW : HIGH);
}
static void ledColor(uint32_t c) {
  bool r = ((c >> 16) & 255) > 110, g = ((c >> 8) & 255) > 110, b = (c & 255) > 110;
  if (!r && !g && !b) r = g = b = true;
  led(r, g, b);
}

// ----------------------------------------------------------------- settings
static bool nightNow() {
  if (!settings.night || !timeValid()) return false;
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  int h = tm.tm_hour, s = settings.nightStart, e = settings.nightEnd;
  return s <= e ? (h >= s && h < e) : (h >= s || h < e);
}
static uint8_t targetBrightness() { return nightNow() ? settings.nightBrightness : settings.brightness; }

void applySettings() {
  configTzTime(settings.tz.c_str(), "pool.ntp.org", "time.google.com");
  lcd.setRotation(settings.flip ? 3 : 1);
  lcd.invertDisplay(settings.invert);
  lcd.setBrightness(max<uint8_t>(targetBrightness(), 4));
}

void requestRecalibration() {
  Preferences p;
  p.begin("touch", false);
  p.putBool("recal", true);
  p.end();
}

// ----------------------------------------------------------------- touch
static void setupTouch() {
  Preferences p;
  p.begin("touch", false);
  uint16_t cal[8];
  bool have = p.getBytesLength("cal") == sizeof(cal);
  int32_t x, y;
  bool held = lcd.getTouch(&x, &y);
  if (have && !held && !p.getBool("recal", false)) {
    p.getBytes("cal", cal, sizeof(cal));
    lcd.setTouchCalibrate(cal);
  } else {
    View v = viewFor(nullptr);
    drawMessage(gfx, v, "Touch calibration", "Tap each arrow as it appears");
    delay(2500);
    lcd.fillScreen(0x000000u);
    lcd.calibrateTouch(cal, 0xFFFFFFu, 0x000000u, 24);
    p.putBytes("cal", cal, sizeof(cal));
    p.putBool("recal", false);
  }
  p.end();
}

// ----------------------------------------------------------------- fetching (core 0)
static uint32_t eventInterval(const Game& g) {
  if (!g.valid) return 60000;
  if (strcmp(g.state, "in") == 0) return 15000;
  if (strcmp(g.state, "pre") == 0) {
    long until = (long)difftime(g.start, time(nullptr));
    if (until < 20 * 60) return 30000;
    if (until < 3 * 3600) return 5 * 60000;
    return 15 * 60000;
  }
  return 30 * 60000;
}

// Queue every logo this team's screens will need, so they're cached before they're drawn
// (otherwise the placeholder badge shows until the first draw triggers the download).
static void prefetchLogos(size_t i) {
  struct L {
    String path;
    int size;
    bool dark;
  };
  std::vector<L> want;
  {
    Lock l;
    if (i >= teams.size()) return;
    const FavTeam& t = teams[i];
    bool dark = settings.theme == 0;
    static const int card[3] = {40, 48, 56};
    int cs = card[settings.logoSize > 2 ? 2 : settings.logoSize];
    want.push_back({t.logo, 32, dark});  // My Teams row
    if (t.game.valid) {
      for (const Side* s : {&t.game.home, &t.game.away}) want.push_back({s->logo, cs, dark});
      if (const Side* f = t.favSide()) want.push_back({f->logo, 80, lum(f->color) < 120});  // score alert
    } else {
      want.push_back({t.logo, cs, dark});
    }
  }
  for (auto& w : want) logoPrefetch(w.path.c_str(), w.size, w.dark);
}

static void fetchTask(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    logoService();
    uint32_t gen = configGen;
    size_t n;
    bool forced = refreshRequest;
    {
      Lock l;
      n = teams.size();
      if (forced) {
        refreshRequest = false;
        uint32_t now = millis();
        for (auto& t : teams) {
          t.nextTeamFetch = now;
          t.nextEventFetch = now;
        }
      }
    }
    for (size_t i = 0; i < n; i++) {
      String sport, league, id;
      char eventId[16];
      uint32_t nextTeam, nextEvent;
      {
        Lock l;
        if (gen != configGen || i >= teams.size()) break;
        FavTeam& t = teams[i];
        sport = t.sport;
        league = t.league;
        id = t.id;
        strlcpy(eventId, t.game.eventId, sizeof eventId);
        nextTeam = t.nextTeamFetch;
        nextEvent = t.nextEventFetch;
      }
      uint32_t now = millis();

      if (nextTeam == 0 || (int32_t)(now - nextTeam) >= 0) {
        TeamInfo info;
        bool ok = espnFetchTeam(sport, league, id, info);
        Lock l;
        if (gen != configGen) break;
        FavTeam& t = teams[i];
        t.nextTeamFetch = millis() + (ok ? 30 * 60000 : 60000);
        if (ok) {
          lastFetchOk = millis();
          lastUpdated = time(nullptr);
          strlcpy(t.record, info.record, sizeof t.record);
          t.noUpcoming = info.nextEventId[0] == 0 && !t.game.valid;
          bool holdFinal = strcmp(t.game.state, "post") == 0 && t.finishedAt &&
                           difftime(time(nullptr), t.finishedAt) < 4 * 3600;
          if (info.nextEventId[0] && strcmp(info.nextEventId, t.game.eventId) != 0 && !t.isLive() && !holdFinal) {
            t.game = Game();
            strlcpy(t.game.eventId, info.nextEventId, sizeof t.game.eventId);
            t.nextEventFetch = 0;
            t.lastFavScore = t.lastOppScore = -1;
            t.finishedAt = 0;
            strlcpy(eventId, info.nextEventId, sizeof eventId);
            nextEvent = 0;
          }
          if (!info.nextEventId[0]) t.loaded = true;
          dataDirty = true;
        }
      }

      now = millis();
      if (eventId[0] && (nextEvent == 0 || (int32_t)(now - nextEvent) >= 0)) {
        Game g;
        bool ok = espnFetchEvent(sport, league, eventId, g);
        Lock l;
        if (gen != configGen) break;
        FavTeam& t = teams[i];
        t.loaded = true;
        if (ok && strcmp(g.eventId, t.game.eventId) == 0) {
          lastFetchOk = millis();
          lastUpdated = time(nullptr);
          bool wasLive = t.isLive();
          t.game = g;
          t.noUpcoming = false;
          const Side* f = t.favSide();
          const Side* o = t.oppSide();
          if (f && o && t.isLive() && g.sport != SP_BASKETBALL) {  // basketball scores too often to celebrate
            int fs = atoi(f->score), os = atoi(o->score);
            if (t.lastFavScore >= 0 && fs > t.lastFavScore) pendingScore = ScoreEvent(i, fs - t.lastFavScore, false);
            else if (t.lastOppScore >= 0 && os > t.lastOppScore) pendingScore = ScoreEvent(i, os - t.lastOppScore, true);
          }
          if (f && o) {
            t.lastFavScore = atoi(f->score);
            t.lastOppScore = atoi(o->score);
          }
          if (wasLive && strcmp(g.state, "post") == 0) {
            t.finishedAt = time(nullptr);
            t.nextTeamFetch = millis() + 4 * 3600 * 1000UL;
          }
          dataDirty = true;
        }
        t.nextEventFetch = millis() + (ok ? eventInterval(t.game) : 60000);
      }
      prefetchLogos(i);
      logoService();
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (forced) refreshDone = true;
    vTaskDelay(pdMS_TO_TICKS(400));
  }
}

// ----------------------------------------------------------------- boot screen
static BootInfo boot;

static void bootInit() {
  boot.label[0] = "Wi-Fi";
  boot.label[1] = "Clock";
  boot.label[2] = "Scores";
  for (int i = 0; i < 3; i++) {
    boot.state[i] = 0;
    boot.detail[i] = "";
  }
  boot.progress = 0;
  boot.teams.clear();
  Lock l;
  for (auto& t : teams) boot.teams.push_back({t.logo, t.abbr, t.color, false});
}

static void bootDraw() {
  logoFlush();
  View v = viewFor(teams.empty() ? nullptr : &teams[0]);
  drawBoot(gfx, v, boot, millis());
}

// ----------------------------------------------------------------- wifi
static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  // Try the saved network ourselves first, patiently (routers can take a while after a power cut).
  // Handing straight to WiFiManager raced the ESP's own auto-connect ("sta is connecting") and gave
  // up after ~5 s, dropping the ticker into setup mode for 5 minutes.
  WiFi.begin();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) {
    bootDraw();
    delay(30);
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
    return;
  }
  WiFi.disconnect();
  WiFiManager wm;
  wm.setTitle("Sports Ticker");
  wm.setConnectTimeout(20);
  wm.setConnectRetries(3);          // routers can be slow right after a power cut
  // nobody configured it: reboot and retry. With a saved network (router just slow / briefly down)
  // retry after 2 min; a brand-new ticker waits 5 min for someone to set it up.
  wm.setConfigPortalTimeout(wm.getWiFiIsSaved() ? 120 : 300);
  wm.setAPCallback([](WiFiManager*) { drawWifiSetup(gfx, viewFor(nullptr), AP_NAME); });
  if (!wm.autoConnect(AP_NAME)) {
    drawMessage(gfx, viewFor(nullptr), "Wi-Fi failed", "Restarting\xE2\x80\xA6");
    delay(2000);
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
}

// ----------------------------------------------------------------- pages
enum class Screen { Pages, Info, Demo };
static Screen screen = Screen::Pages;
static int page = 0;
static uint32_t lastFlip = 0, lastTouch = 0;
static int lastMinute = -1;
static Banner banner;
static uint32_t bannerUntil = 0;
static int demoScene = 0;
static uint32_t demoSceneAt = 0;

struct PageRef {
  bool summary;
  int index;  // team index, or first team on a summary page
};

static std::vector<PageRef> buildPages(size_t n) {
  std::vector<PageRef> p;
  if (n > 1 && settings.showSummary)
    for (size_t i = 0; i < n; i += 4) p.push_back({true, (int)i});
  for (size_t i = 0; i < n; i++) p.push_back({false, (int)i});
  return p;
}

static String ipStr() { return WiFi.localIP().toString(); }

static void drawPage() {
  std::vector<FavTeam> snap;
  {
    Lock l;
    snap = teams;
  }
  if (snap.empty()) {
    drawSetup(gfx, viewFor(nullptr), ipStr());
    return;
  }
  auto pages = buildPages(snap.size());
  if (page >= (int)pages.size()) page = 0;
  const PageRef& pr = pages[page];
  if (pr.summary) {
    drawSummary(gfx, viewFor(&snap[0]), snap, pr.index, page, pages.size());
  } else {
    const FavTeam& t = snap[pr.index];
    bool showBanner = banner.on && millis() < bannerUntil;
    drawCard(gfx, viewFor(&t), t, page, pages.size(), showBanner ? &banner : nullptr);
  }
}

static void autoAdvance() {
  std::vector<int> live;
  size_t n;
  {
    Lock l;
    n = teams.size();
    for (size_t i = 0; i < n; i++)
      if (teams[i].isLive()) live.push_back(i);
  }
  auto pages = buildPages(n);
  if (pages.size() <= 1) return;
  if (!live.empty()) {  // while anything is live, cycle only the live games
    int cur = pages[page].summary ? -1 : pages[page].index, next = live[0];
    for (int idx : live)
      if (idx > cur) {
        next = idx;
        break;
      }
    for (size_t p = 0; p < pages.size(); p++)
      if (!pages[p].summary && pages[p].index == next) page = p;
  } else {
    page = (page + 1) % pages.size();
  }
}

static int pageOfTeam(int idx) {
  size_t n;
  {
    Lock l;
    n = teams.size();
  }
  auto pages = buildPages(n);
  for (size_t p = 0; p < pages.size(); p++)
    if (!pages[p].summary && pages[p].index == idx) return p;
  return 0;
}

// ----------------------------------------------------------------- demo
static const char* DEMO_TAGS[D_COUNT] = {"DEMO 1/13", "DEMO 2/13", "DEMO 3/13", "DEMO 4/13", "DEMO 5/13",
                                         "DEMO 6/13", "DEMO 7/13", "DEMO 8/13", "DEMO 9/13", "DEMO 10/13",
                                         "DEMO 11/13", "DEMO 12/13", "DEMO 13/13"};

static void demoDraw(uint32_t ms) {
  const auto& T = demoTeams();
  demoTag = DEMO_TAGS[demoScene];
  uint32_t t = ms - demoSceneAt;
  switch (demoScene) {
    case D_BOOT: {
      BootInfo b;
      b.label[0] = "Wi-Fi";
      b.label[1] = "Clock";
      b.label[2] = "Scores";
      b.state[0] = b.state[1] = 2;
      b.state[2] = 1;
      b.detail[0] = WiFi.SSID();
      b.detail[1] = timeValid() ? fmtClock(time(nullptr), settings.use24h) : String("");
      int loaded = (t / 1200) % 4;
      b.detail[2] = String(loaded) + " of 3 teams";
      b.progress = 0.4f + 0.2f * loaded;
      b.teams = {{T[0].logo, T[0].abbr, T[0].color, loaded > 0},
                 {T[1].logo, T[1].abbr, T[1].color, loaded > 1},
                 {T[2].logo, T[2].abbr, T[2].color, loaded > 2}};
      drawBoot(gfx, viewFor(&T[0]), b, ms);
      break;
    }
    case D_LIVE_NFL: drawCard(gfx, viewFor(&T[DT_BRONCOS_LIVE]), T[DT_BRONCOS_LIVE], 1, 4); break;
    case D_LIVE_NHL: drawCard(gfx, viewFor(&T[DT_AVS_LIVE]), T[DT_AVS_LIVE], 2, 4); break;
    case D_LIVE_NBA: drawCard(gfx, viewFor(&T[DT_NUGGETS_LIVE]), T[DT_NUGGETS_LIVE], 1, 4); break;
    case D_LIVE_MLB: drawCard(gfx, viewFor(&T[DT_ROCKIES_LIVE]), T[DT_ROCKIES_LIVE], 1, 4); break;
    case D_UPCOMING: drawCard(gfx, viewFor(&T[DT_AVS_UPCOMING]), T[DT_AVS_UPCOMING], 2, 4); break;
    case D_FINAL: drawCard(gfx, viewFor(&T[DT_RAMS_FINAL]), T[DT_RAMS_FINAL], 3, 4); break;
    case D_SUMMARY: {
      std::vector<FavTeam> three(T.begin(), T.begin() + 3);
      drawSummary(gfx, viewFor(&T[0]), three, 0, 0, 4);
      break;
    }
    case D_CELEBRATE: {
      float p = (t % 2400) / 1200.0f;
      drawCelebrate(gfx, viewFor(&T[0]), T[0], "TOUCHDOWN", p > 1 ? 1 : p);
      break;
    }
    case D_OPP_ALERT: {
      Banner b;
      b.on = true;
      b.color = T[0].game.away.color;
      b.text = "KC scores \xC2\xB7 17 \xE2\x80\x93 21";
      drawCard(gfx, viewFor(&T[0]), T[0], 1, 4, &b);
      break;
    }
    case D_SETUP: drawSetup(gfx, viewFor(nullptr), ipStr()); break;
    case D_INFO: drawInfo(gfx, viewFor(nullptr), WiFi.SSID(), WiFi.RSSI(), ipStr(), 12, targetBrightness()); break;
    case D_LIGHT: {
      View v = viewFor(&T[0]);
      v.dark = false;
      v.th = &THEME_LIGHT;
      drawCard(gfx, v, T[0], 1, 4);
      break;
    }
  }
  demoTag = nullptr;
}

static bool demoAnimated() { return demoScene == D_BOOT || demoScene == D_CELEBRATE; }

static void enterDemo() {
  screen = Screen::Demo;
  demoScene = 0;
  demoSceneAt = millis();
  dataDirty = true;
}

static void showInfo(bool sliderOnly = false) {
  long ago = lastFetchOk ? (long)((millis() - lastFetchOk) / 1000) : -1;
  drawInfo(gfx, viewFor(nullptr), WiFi.SSID(), WiFi.RSSI(), ipStr(), ago, targetBrightness(), sliderOnly);
}

static void exitDemo() {
  screen = Screen::Pages;
  dataDirty = true;
}

// ----------------------------------------------------------------- touch handling
static constexpr uint32_t HOLD_DELAY = 350;  // ms before a press counts as a hold

static void handleTouch() {
  static bool down = false, longFired = false, sliding = false;
  static uint32_t t0 = 0, lastSlideDraw = 0;
  static int32_t tx = 0, ty = 0;
  static int32_t sumX = 0, sumY = 0, nSamples = 0;  // a tap acts on the average position (lift-off samples jitter)
  static uint32_t lastSeen = 0;
  int32_t x, y;
  bool touching = lcd.getTouch(&x, &y);
  if (touching) lastSeen = millis();
  // the resistive panel briefly loses contact during a drag: only count a release after 80 ms
  if (!touching && down && millis() - lastSeen < 80) return;
  if (touching) {
    if (!down) {
      down = true;
      longFired = false;
      t0 = millis();
      sumX = sumY = nSamples = 0;
      // on the info screen, a touch that starts on the brightness bar drags it
      sliding = screen == Screen::Info && x <= INFO_SLIDER_TOUCH_X1 && y >= INFO_SLIDER_TOUCH_Y0;
    }
    tx = x;
    ty = y;
    if (nSamples < 64) {
      sumX += x;
      sumY += y;
      nSamples++;
    }
    if (sliding) {
      int b = 10 + (x - INFO_SLIDER_X0) * 245 / (INFO_SLIDER_X1 - INFO_SLIDER_X0);
      b = constrain(b, 10, 255);
      {
        Lock l;
        if (nightNow()) settings.nightBrightness = b;
        else settings.brightness = b;
      }
      lcd.setBrightness(b);
      if (millis() - lastSlideDraw > 40) {
        lastSlideDraw = millis();
        showInfo(true);
      }
      return;
    }
    uint32_t held = millis() - t0;
    if (screen == Screen::Pages && !longFired && hdrStatus.mode != HDR_LOADING) {
      // hold to refresh: a 3 s countdown in the header (after a short pause so taps stay taps)
      static uint32_t lastHdrDraw = 0;
      if (held > HOLD_DELAY) {
        hdrStatus.mode = HDR_COUNTDOWN;
        hdrStatus.prog = (held - HOLD_DELAY) / 3000.0f;
        if (hdrStatus.prog >= 1) {
          longFired = true;
          hdrStatus.mode = HDR_LOADING;
          hdrStatus.phase = 0;
          refreshDone = false;
          refreshRequest = true;
          refreshStarted = millis();
        }
        if (millis() - lastHdrDraw > 40 || longFired) {
          lastHdrDraw = millis();
          redrawHeader(gfx);
        }
      }
      return;
    }
    if (!longFired && held > 1200 && screen != Screen::Pages) {
      longFired = true;
      if (screen == Screen::Info) {
        enterDemo();
      } else {
        exitDemo();
      }
    }
    return;
  }
  if (!down) return;
  down = false;
  if (sliding) {  // finished dragging: draw the final position and remember it
    sliding = false;
    showInfo(true);
    saveSettingsOnly();
    return;
  }
  if (longFired) return;
  if (nSamples) {
    tx = sumX / nSamples;
    ty = sumY / nSamples;
  }
  if (hdrStatus.mode == HDR_COUNTDOWN) {  // let go before the countdown finished: cancel
    hdrStatus.mode = HDR_NORMAL;
    redrawHeader(gfx);
    return;
  }
  lastTouch = lastFlip = millis();
  if (screen == Screen::Info) {
    // only the close button closes it (generous hit area: the top-right corner)
    if (tx < INFO_CLOSE_CX - INFO_CLOSE_R - 16 || ty > INFO_CLOSE_CY + INFO_CLOSE_R + 14) return;
    screen = Screen::Pages;
  } else if (screen == Screen::Pages && tx >= 256 && ty >= 216) {  // gear in the corner
    screen = Screen::Info;
    showInfo();
    return;
  } else if (screen == Screen::Demo) {
    demoScene = (tx < 107) ? (demoScene + D_COUNT - 1) % D_COUNT : (demoScene + 1) % D_COUNT;
    demoSceneAt = millis();
  } else {
    size_t n;
    {
      Lock l;
      n = teams.size();
    }
    auto pl = buildPages(n);
    int pages = pl.size();
    // My Teams: tapping a row opens that team's game
    int row = -1;
    if (page < pages && pl[page].summary) {
      int first = pl[page].index;
      row = summaryRowAt(min<int>(4, n - first), ty);
      if (row >= 0) page = pageOfTeam(first + row);
    }
    if (row < 0 && pages > 1) page = (tx < 107) ? (page + pages - 1) % pages : (page + 1) % pages;
  }
  dataDirty = true;
}

// ----------------------------------------------------------------- score alerts
static void handleScoreEvent() {
  ScoreEvent ev;
  FavTeam t;
  {
    Lock l;
    ev = pendingScore;
    pendingScore = ScoreEvent();
    if (ev.idx < 0 || ev.idx >= (int)teams.size()) return;
    t = teams[ev.idx];
  }
  if (screen != Screen::Pages) return;
  page = pageOfTeam(ev.idx);
  lastFlip = millis();
  const Side* f = t.favSide();
  const Side* o = t.oppSide();
  if (!f || !o) return;
  if (!ev.opponent && settings.celebrate) {
    View v = viewFor(&t);
    const char* word = celebrationWord(t.game.sport, ev.delta);
    if (settings.led) ledColor(f->color);
    uint32_t s = millis();
    while (millis() - s < 1200) {
      drawCelebrate(gfx, v, t, word, (millis() - s) / 1200.0f);
      if (settings.led) ledColor(((millis() - s) / 150) % 2 ? f->alt : f->color);
    }
    drawCelebrate(gfx, v, t, word, 1.0f);
    delay(1800);
    led(false, false, false);
  } else if (ev.opponent && settings.oppAlert) {
    banner.on = true;
    banner.color = o->color;
    banner.text = String(o->abbr) + " scores \xC2\xB7 " + o->score + " \xE2\x80\x93 " + f->score;
    bannerUntil = millis() + 4000;
  }
  dataDirty = true;
}

// ----------------------------------------------------------------- arduino
void setup() {
  Serial.begin(115200);
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  led(false, false, false);

  configInit();
  lcd.init();
  applySettings();
  gfx.begin();
  logosBegin();
  setupTouch();

  bootInit();
  boot.state[0] = 1;
  boot.detail[0] = "connecting";
  bootDraw();
  connectWifi();
  boot.state[0] = 2;
  boot.detail[0] = WiFi.SSID();
  boot.state[1] = 1;
  boot.progress = 0.33f;
  applySettings();  // starts NTP now that we're online

  webBegin();
  xTaskCreatePinnedToCore(fetchTask, "fetch", 20480, nullptr, 1, &fetchHandle, 0);

  uint32_t t0 = millis();
  while (!timeValid() && millis() - t0 < 10000) {
    bootDraw();
    delay(30);
  }
  boot.state[1] = timeValid() ? 2 : 3;
  boot.detail[1] = timeValid() ? fmtClock(time(nullptr), settings.use24h) : String("no time server");
  boot.state[2] = 1;

  // wait for the first round of scores (or give up after 20 s)
  t0 = millis();
  for (;;) {
    int total = 0, done = 0;
    {
      Lock l;
      total = teams.size();
      for (size_t i = 0; i < teams.size() && i < boot.teams.size(); i++) {
        boot.teams[i].loaded = teams[i].loaded;
        if (teams[i].loaded) done++;
      }
    }
    if (total == 0) break;
    boot.detail[2] = String(done) + " of " + total + (total == 1 ? " team" : " teams");
    boot.progress = 0.66f + 0.34f * done / total;
    bootDraw();
    webLoop();
    if (done == total || millis() - t0 > 20000) break;
    delay(30);
  }
  // then for the logos those games need (first boot downloads them; after that they're cached)
  t0 = millis();
  while ((millis() - t0 < 500 || logoPending() > 0) && millis() - t0 < 15000) {  // 500 ms: let the last team queue its logos
    if (logoPending()) boot.detail[2] = String("logos \xC2\xB7 ") + logoPending() + " left";
    bootDraw();
    webLoop();
    delay(30);
  }
  boot.state[2] = 2;
  boot.progress = 1;
  bootDraw();
  delay(400);

  lastFlip = millis();
  dataDirty = true;
}

void loop() {
  webLoop();
  handleTouch();
  if (logoFlush()) dataDirty = true;
  uint32_t now = millis();

  static uint32_t lastBright = 0;
  if (now - lastBright > 10000) {
    lastBright = now;
    lcd.setBrightness(max<uint8_t>(targetBrightness(), 4));
  }

  // keep the info screen's "Scores updated" line current
  static uint32_t lastInfoDraw = 0;
  int32_t tx_, ty_;
  if (screen == Screen::Info && now - lastInfoDraw > 10000 && !lcd.getTouch(&tx_, &ty_)) {
    lastInfoDraw = now;
    showInfo();
  }

  if (demoRequest) {
    demoRequest = false;
    enterDemo();
  }
  if (demoExitRequest) {
    demoExitRequest = false;
    if (screen == Screen::Demo) exitDemo();
  }

  if (screen == Screen::Demo) {
    static uint32_t lastFrame = 0;
    if (!demoAnimated() && now - demoSceneAt > 7000 && now - lastTouch > 7000) {
      demoScene = (demoScene + 1) % D_COUNT;
      demoSceneAt = now;
      dataDirty = true;
    }
    if (demoAnimated() && now - demoSceneAt > 6000 && now - lastTouch > 6000) {
      demoScene = (demoScene + 1) % D_COUNT;
      demoSceneAt = now;
      dataDirty = true;
    }
    if (dataDirty || (demoAnimated() && now - lastFrame > 40)) {
      dataDirty = false;
      lastFrame = now;
      demoDraw(now);
    }
  } else if (screen == Screen::Pages) {
    bool pending;
    {
      Lock l;
      pending = pendingScore.idx >= 0;
    }
    if (pending) handleScoreEvent();

    if (banner.on && now > bannerUntil) {
      banner.on = false;
      dataDirty = true;
    }
    bool idle = now - lastTouch > 30000;
    if (idle && now - lastFlip > settings.rotateSec * 1000UL && !banner.on) {
      lastFlip = now;
      int before = page;
      autoAdvance();
      if (page != before) dataDirty = true;
    }
    if (timeValid()) {
      time_t t = time(nullptr);
      struct tm tm;
      localtime_r(&t, &tm);
      if (tm.tm_min != lastMinute) {
        lastMinute = tm.tm_min;
        dataDirty = true;  // header clock + countdowns
      }
    }
    if (dataDirty) {
      dataDirty = false;
      drawPage();
    }
  }

  // refresh spinner in the header until the fetch task finishes the forced round
  if (hdrStatus.mode == HDR_LOADING) {
    static uint32_t lastSpin = 0;
    if (refreshDone || now - refreshStarted > 30000) {
      hdrStatus.mode = HDR_NORMAL;
      refreshDone = false;
      dataDirty = true;
    } else if (now - lastSpin > 40) {
      lastSpin = now;
      hdrStatus.phase = fmodf((now - refreshStarted) * 0.45f, 360.0f);
      if (screen == Screen::Pages) redrawHeader(gfx);
    }
  }

  // pulse the LIVE dot (tiny region, drawn straight to the panel)
  if (screen != Screen::Info && liveDot.on) {
    static uint32_t lastPulse = 0;
    if (now - lastPulse > 50) {
      lastPulse = now;
      float p = 0.5f + 0.5f * sinf(now / 260.0f);
      lcd.fillCircle(liveDot.x, liveDot.y, 4, liveDot.bg);
      lcd.fillCircle(liveDot.x, liveDot.y, 3, mix(liveDot.color, liveDot.bg, 0.3f + 0.7f * p));
    }
  }
  delay(5);
}
