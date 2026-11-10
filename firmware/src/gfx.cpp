#include "gfx.h"
#include "logos.h"

const Theme THEME_DARK = {0x0B0B0Du, 0x18181Bu, 0x2C2C31u, 0x2C2C31u, 0xF5F5F7u,
                          0xA1A1AAu, 0x8E8E96u, 0x3F3F46u, 0xFF453Au, 0x30D158u};
const Theme THEME_LIGHT = {0xF2F2F5u, 0xFFFFFFu, 0xE2E2E8u, 0xE2E2E8u, 0x111113u,
                           0x5C5C63u, 0x6B6B73u, 0xC7C7CEu, 0xD70015u, 0x1F9D44u};

uint8_t lum(uint32_t c) {
  return (uint8_t)((((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000);
}
uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) { return (uint32_t)(((a >> s) & 255) * t + ((b >> s) & 255) * (1 - t)) & 255; };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
uint32_t readableOn(uint32_t bg) { return lum(bg) > 150 ? 0x111113u : 0xFFFFFFu; }

// pixel size of each generated font (for vertical centering)
static int fontPx(const uint8_t* f) {
  if (f == INTER_11) return 11;
  if (f == INTER_12) return 12;
  if (f == INTER_13 || f == INTER_13B) return 13;
  if (f == INTER_15) return 15;
  if (f == INTER_17) return 17;
  if (f == INTER_30) return 30;
  if (f == INTER_40) return 40;
  return 12;
}

void Gfx::begin() {
  spr.setColorDepth(16);
  spr.createSprite(W, STRIP);
  spr.setTextWrap(false);
}

void Gfx::font(const uint8_t* f) {
  if (f == cur) return;
  if (cur) spr.unloadFont();
  spr.loadFont(f);
  cur = f;
}

void Gfx::rect(int x, int y, int w, int h, uint32_t c) {
  if (vis(y, h)) spr.fillRect(x, y - oy, w, h, c);
}
void Gfx::rrect(int x, int y, int w, int h, int r, uint32_t c) {
  if (vis(y, h)) spr.fillSmoothRoundRect(x, y - oy, w, h, r, c);
}
void Gfx::circle(int cx, int cy, int r, uint32_t c) {
  if (vis(cy - r, 2 * r + 1)) spr.fillSmoothCircle(cx, cy - oy, r, c);
}
void Gfx::ring(int cx, int cy, int r, int t, uint32_t c) {
  if (vis(cy - r, 2 * r + 1)) spr.fillArc(cx, cy - oy, r, r - t, 0, 360, c);
}
void Gfx::line(int x0, int y0, int x1, int y1, float r, uint32_t c) {
  int top = min(y0, y1) - (int)r - 1, bot = max(y0, y1) + (int)r + 1;
  if (vis(top, bot - top + 1)) spr.drawWideLine(x0, y0 - oy, x1, y1 - oy, r, c);
}
void Gfx::tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c) {
  int top = min(y0, min(y1, y2)), bot = max(y0, max(y1, y2));
  if (vis(top, bot - top + 1)) spr.fillTriangle(x0, y0 - oy, x1, y1 - oy, x2, y2 - oy, c);
}
void Gfx::ellipse(int cx, int cy, int rx, int ry, uint32_t c) {
  if (vis(cy - ry, 2 * ry + 1)) spr.fillEllipse(cx, cy - oy, rx, ry, c);
}
void Gfx::arc(int cx, int cy, int r0, int r1, float a0, float a1, uint32_t c) {
  if (vis(cy - r0, 2 * r0 + 1)) spr.fillArc(cx, cy - oy, r0, r1, a0, a1, c);
}

int Gfx::width(const char* s, const uint8_t* f) {
  font(f);
  return spr.textWidth(s);
}

String Gfx::fit(const char* s, const uint8_t* f, int maxw) {
  String str = s ? s : "";
  if (maxw <= 0 || width(str, f) <= maxw) return str;
  const char* ell = "\xE2\x80\xA6";  // "…"
  while (str.length() > 0) {
    // drop one UTF-8 character
    int i = str.length() - 1;
    while (i > 0 && (str[i] & 0xC0) == 0x80) i--;
    str.remove(i);
    str.trim();
    if (width(str + ell, f) <= maxw) break;
  }
  return str + ell;
}

int Gfx::text(const char* s, int x, int cy, const uint8_t* f, uint32_t color, Align al, int maxw) {
  if (!s || !*s) return 0;
  int px = fontPx(f);
  if (!vis(cy - px, 2 * px)) {
    return maxw > 0 ? min(maxw, width(s, f)) : width(s, f);
  }
  String str = fit(s, f, maxw);
  font(f);
  spr.setTextColor(color);
  spr.setTextDatum(al == AL_L ? textdatum_t::baseline_left
                              : (al == AL_C ? textdatum_t::baseline_center : textdatum_t::baseline_right));
  int baseline = cy + (int)lroundf(px * 0.36f);  // Inter cap height ~0.73em
  spr.drawString(str, x, baseline - oy);
  return spr.textWidth(str);
}

void Gfx::logo(const char* path, int size, int x, int y, bool dark, uint32_t color, const char* abbr) {
  if (!vis(y, size)) return;
  if (logoDraw(spr, path, size, dark, x, y - oy, 0, STRIP)) return;
  // placeholder until the logo arrives
  int r = size / 2 - 2;
  spr.fillSmoothCircle(x + size / 2, y + size / 2 - oy, r, color);
  const uint8_t* f = size >= 48 ? INTER_15 : INTER_11;
  text(abbr, x + size / 2, y + size / 2, f, readableOn(color), AL_C, size - 6);
}

void Gfx::qr(const char* txt, int x, int y, int w) {
  if (vis(y, w)) spr.qrcode(txt, x, y - oy, w, 3);
}
