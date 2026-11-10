// Flicker-free renderer: every frame is drawn into an 80-row sprite three times
// (one per horizontal strip) and pushed to the panel. A full 320x240 16-bit
// buffer would need 150 KB, which we can't spare next to HTTPS.
#pragma once
#include <vector>
#include "fonts_inter.h"
#include "lgfx_cyd.h"

struct Theme {
  uint32_t bg, surface, track, line, text, sub, dim, dot, live, win;
};
extern const Theme THEME_DARK, THEME_LIGHT;

enum Align : uint8_t { AL_L, AL_C, AL_R };

uint8_t lum(uint32_t c);
uint32_t mix(uint32_t a, uint32_t b, float t);  // t=1 -> a
uint32_t readableOn(uint32_t bg);              // black or white text

class Gfx {
 public:
  static constexpr int W = 320, H = 240, STRIP = 80;
  explicit Gfx(LGFX& l) : lcd(l), spr(&l) {}
  void begin();

  template <class F>
  void frame(uint32_t bg, F draw) {
    for (oy = 0; oy < H; oy += STRIP) {
      spr.fillScreen(bg);
      draw();
      spr.pushSprite(0, oy);
    }
    oy = 0;
  }
  // redraw a small rectangle (h <= 80) in place, e.g. the header while an animation runs
  template <class F>
  void region(uint32_t bg, int x, int y, int w, int h, F draw) {
    oy = y;
    spr.fillRect(x, 0, w, h, bg);
    draw();
    lcd.setClipRect(x, y, w, h);
    spr.pushSprite(0, oy);
    lcd.clearClipRect();
    oy = 0;
  }
  // redraw just one 80-row strip (0..2): cheap partial updates, e.g. a slider being dragged
  template <class F>
  void strip(uint32_t bg, int idx, F draw) {
    oy = idx * STRIP;
    spr.fillScreen(bg);
    draw();
    spr.pushSprite(0, oy);
    oy = 0;
  }

  void rect(int x, int y, int w, int h, uint32_t c);
  void rrect(int x, int y, int w, int h, int r, uint32_t c);
  void circle(int cx, int cy, int r, uint32_t c);
  void ring(int cx, int cy, int r, int thick, uint32_t c);
  void line(int x0, int y0, int x1, int y1, float r, uint32_t c);  // anti-aliased, r = half thickness
  void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c);
  void ellipse(int cx, int cy, int rx, int ry, uint32_t c);
  void arc(int cx, int cy, int r0, int r1, float a0, float a1, uint32_t c);
  // y = vertical center of the text line (caps). maxw > 0 truncates with "…". Returns width drawn.
  int text(const char* s, int x, int cy, const uint8_t* font, uint32_t color, Align al = AL_L, int maxw = 0);
  int text(const String& s, int x, int cy, const uint8_t* font, uint32_t color, Align al = AL_L, int maxw = 0) {
    return text(s.c_str(), x, cy, font, color, al, maxw);
  }
  int width(const char* s, const uint8_t* font);
  int width(const String& s, const uint8_t* font) { return width(s.c_str(), font); }
  String fit(const char* s, const uint8_t* font, int maxw);
  // team logo from cache; draws a colored badge with the abbreviation until it's downloaded
  void logo(const char* path, int size, int x, int y, bool dark, uint32_t color, const char* abbr);
  void qr(const char* txt, int x, int y, int w);

  LGFX& lcd;
  LGFX_Sprite spr;
  int oy = 0;

 private:
  bool vis(int y, int h) const { return y + h > oy && y < oy + STRIP; }
  void font(const uint8_t* f);
  const uint8_t* cur = nullptr;
};
