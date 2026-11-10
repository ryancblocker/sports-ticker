// Every screen of the ticker, drawn to match the design canvas (320x240, 1:1 pixels).
#pragma once
#include <vector>
#include "gfx.h"
#include "model.h"

struct View {
  const Theme* th;
  bool dark;
  uint32_t accent;
  int logo;  // 40 / 48 / 56
  bool records, situation, clock, tv, countdown, linescore, h24;
};
View viewFor(const FavTeam* t);  // built from settings; accent may follow the team

struct BootTeam {
  String logo, abbr;
  uint32_t color;
  bool loaded;
};
struct BootInfo {
  const char* label[3];
  String detail[3];
  uint8_t state[3];  // 0 waiting, 1 working, 2 done, 3 problem
  float progress;    // 0..1
  std::vector<BootTeam> teams;
};

struct Banner {
  bool on = false;
  uint32_t color = 0;
  String text;
};

// Where the pulsing live dot was drawn (main.cpp animates it directly on the panel).
struct LiveDot {
  bool on = false;
  int x = 0, y = 0;
  uint32_t bg = 0, color = 0;
};
extern LiveDot liveDot;

// Right side of the header: "Last updated 7:38 PM", or the hold-to-refresh countdown / spinner.
enum HdrMode : uint8_t { HDR_NORMAL, HDR_COUNTDOWN, HDR_LOADING };
struct HdrStatus {
  HdrMode mode = HDR_NORMAL;
  float prog = 0;   // countdown: 0..1 over the 3 s hold
  float phase = 0;  // spinner angle, degrees
};
extern HdrStatus hdrStatus;
extern volatile time_t lastUpdated;  // wall-clock time of the last successful fetch (0 = never)
void redrawHeader(Gfx& g);           // repaint only the header of the last card/summary page drawn
extern const char* demoTag;  // non-null in demo mode: shown in the footer

void drawCard(Gfx& g, const View& v, const FavTeam& t, int page, int pages, const Banner* b = nullptr);
void drawSummary(Gfx& g, const View& v, const std::vector<FavTeam>& list, int first, int page, int pages);
int summaryRowAt(int rowsOnPage, int y);  // which My Teams row a touch at y hits, or -1
void drawBoot(Gfx& g, const View& v, const BootInfo& info, uint32_t ms);
void drawCelebrate(Gfx& g, const View& v, const FavTeam& t, const char* word, float t01);
void drawSetup(Gfx& g, const View& v, const String& ip);
// Info screen. bright = current backlight (10..255); sliderOnly redraws just the bottom strip.
constexpr int INFO_CLOSE_CX = 293, INFO_CLOSE_CY = 26, INFO_CLOSE_R = 15;  // close button
constexpr int INFO_SLIDER_X0 = 26, INFO_SLIDER_X1 = 162;           // knob travel
// touch zone: the whole bottom-left of the screen below the info card (once grabbed, the drag
// follows the finger anywhere until it lifts)
constexpr int INFO_SLIDER_TOUCH_Y0 = 165, INFO_SLIDER_TOUCH_X1 = 200;
void drawInfo(Gfx& g, const View& v, const String& ssid, int rssi, const String& ip, long updatedAgoSec,
              int bright, bool sliderOnly = false);
void drawMessage(Gfx& g, const View& v, const char* title, const char* line);
void drawWifiSetup(Gfx& g, const View& v, const char* ap);

const char* celebrationWord(Sport sp, int delta);
String fmtClock(time_t t, bool h24);
String fmtAgo(long sec);  // "just now", "45 s ago", "12 min ago", "3 hr ago"
bool timeValid();
uint32_t teamColor(const Side& s, bool dark);
