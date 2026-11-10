#include "screens.h"
#include <time.h>
#include "config.h"

LiveDot liveDot;
const char* demoTag = nullptr;
HdrStatus hdrStatus;
volatile time_t lastUpdated = 0;
static struct {
  bool valid = false, summary = false;
  String left;
  View v;
} lastHdr;

static constexpr int W = 320;
static constexpr int COLW = 84;           // team column width
static constexpr int NAMEW = 96;          // team name may spill 6 px past the column on each side
static constexpr uint32_t FIELD = 0x1E6B3Au;
static constexpr uint32_t FIRST_DOWN = 0xFFD60Au;
static constexpr uint32_t SCRIMMAGE = 0x4DA3FFu;

// ------------------------------------------------------------------ helpers
bool timeValid() { return time(nullptr) > 1700000000; }

String fmtClock(time_t t, bool h24) {
  struct tm tm;
  localtime_r(&t, &tm);
  char b[12];
  if (h24) snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
  else {
    int h = tm.tm_hour % 12;
    snprintf(b, sizeof b, "%d:%02d %s", h ? h : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
  }
  return b;
}

String fmtAgo(long sec) {
  if (sec < 10) return "just now";
  if (sec < 90) return String(sec) + " s ago";
  if (sec < 90 * 60) return String((sec + 30) / 60) + " min ago";
  return String((sec + 1800) / 3600) + " hr ago";
}

static void splitClock(time_t t, bool h24, String& hm, String& ampm) {
  struct tm tm;
  localtime_r(&t, &tm);
  char b[8];
  if (h24) {
    snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    ampm = "";
  } else {
    int h = tm.tm_hour % 12;
    snprintf(b, sizeof b, "%d:%02d", h ? h : 12, tm.tm_min);
    ampm = tm.tm_hour < 12 ? "AM" : "PM";
  }
  hm = b;
}

// "TONIGHT", "TODAY", "MON" (within the week, tomorrow included), or "SUN NOV 9"
static String fmtDay(time_t start) {
  static const char* D[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* M[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  time_t now = time(nullptr);
  struct tm a, b;
  localtime_r(&now, &a);
  localtime_r(&start, &b);
  struct tm a0 = a, b0 = b;
  a0.tm_hour = b0.tm_hour = 12;
  a0.tm_min = b0.tm_min = a0.tm_sec = b0.tm_sec = 0;
  long dd = lround(difftime(mktime(&b0), mktime(&a0)) / 86400.0);
  if (dd == 0) return b.tm_hour >= 17 ? "TONIGHT" : "TODAY";
  if (dd >= 1 && dd < 7) return D[b.tm_wday];
  char buf[16];
  snprintf(buf, sizeof buf, "%s %s %d", D[b.tm_wday], M[b.tm_mon], b.tm_mday);
  return buf;
}

static String fmtCountdown(time_t start) {
  long d = (long)difftime(start, time(nullptr));
  if (d <= 0) return "starting soon";
  char b[24];
  long days = d / 86400, hrs = (d % 86400) / 3600, mins = (d % 3600) / 60;
  if (days > 0) snprintf(b, sizeof b, "in %ldd %ldh", days, hrs);
  else if (hrs > 0) snprintf(b, sizeof b, "in %ldh %ldm", hrs, mins);
  else snprintf(b, sizeof b, "in %ldm", mins < 1 ? 1 : mins);
  return b;
}

uint32_t teamColor(const Side& s, bool dark) {
  if (dark) {
    if (lum(s.color) >= 45) return s.color;
    if (lum(s.alt) >= 45) return s.alt;
    return 0x8A93A6u;
  }
  if (lum(s.color) <= 200) return s.color;
  if (lum(s.alt) <= 200) return s.alt;
  return 0x5C5C63u;
}

View viewFor(const FavTeam* t) {
  View v;
  v.dark = settings.theme == 0;
  v.th = v.dark ? &THEME_DARK : &THEME_LIGHT;
  if (settings.accent == "team" && t) {
    Side s;
    const Side* f = t->favSide();
    if (f) s = *f;
    else s.color = s.alt = t->color;
    v.accent = teamColor(s, v.dark);
    // a team's alternate color often reads better as an accent (Broncos navy -> orange)
    if (f && lum(f->color) < 60 && lum(f->alt) >= 60 && (!v.dark || lum(f->alt) < 230)) v.accent = f->alt;
  } else if (settings.accent == "team") {
    v.accent = 0xFC4C02u;
  } else {
    v.accent = parseHexColor(settings.accent.c_str(), 0xFC4C02u);
  }
  static const int sizes[] = {40, 48, 56};
  v.logo = sizes[settings.logoSize > 2 ? 2 : settings.logoSize];
  v.records = settings.showRecords;
  v.situation = settings.showSituation;
  v.clock = settings.showClock;
  v.tv = settings.showTV;
  v.countdown = settings.showCountdown;
  v.linescore = settings.showLinescore;
  v.h24 = settings.use24h;
  return v;
}

const char* celebrationWord(Sport sp, int d) {
  switch (sp) {
    case SP_FOOTBALL:
      if (d >= 6) return "TOUCHDOWN";
      if (d == 3) return "FIELD GOAL";
      if (d == 2) return "SAFETY";
      return "SCORE!";
    case SP_HOCKEY:
    case SP_SOCCER:
      return "GOAL!";
    case SP_BASEBALL:
      return d > 1 ? "RUNS SCORE" : "RUN SCORES";
    default:
      return "SCORE!";
  }
}

static bool allDigits(const char* s) {
  if (!*s) return false;
  for (; *s; s++)
    if (*s < '0' || *s > '9') return false;
  return true;
}

static String upper(String s) {
  s.toUpperCase();
  return s;
}

// ------------------------------------------------------------------ shared pieces
// draws the status part right-aligned at `right`; returns its width
static int statusSeg(Gfx& g, const View& v, int right, int cy, int avail) {
  const Theme& th = *v.th;
  if (hdrStatus.mode == HDR_NORMAL) {
    if (!lastUpdated || !timeValid()) return 0;
    String t = fmtClock(lastUpdated, v.h24);
    String opts[3] = {"Last updated " + t, "Updated " + t, t};
    int i = 0;
    while (i < 2 && g.width(opts[i], INTER_11) > avail) i++;
    return g.text(opts[i], right, cy, INTER_11, th.sub, AL_R);
  }
  // countdown ring fills over 3 s, then a spinning arc while the refresh runs
  int cx = right - 6;
  g.ring(cx, cy, 6, 2, th.track);
  String label;
  if (hdrStatus.mode == HDR_COUNTDOWN) {
    float p = constrain(hdrStatus.prog, 0.0f, 1.0f);
    if (p > 0.01f) g.arc(cx, cy, 6, 4, 270, 270 + 360 * p, v.accent);
    label = "Refreshing in " + String(max(1, 3 - (int)(p * 3)));
  } else {
    g.arc(cx, cy, 6, 4, hdrStatus.phase, hdrStatus.phase + 100, v.accent);
    label = "Refreshing\xE2\x80\xA6";
  }
  return 18 + g.text(label, right - 18, cy, INTER_11, th.text, AL_R);
}

// clock (optional) + status on the right; returns the x where the right side begins
static int headerRight(Gfx& g, const View& v, int cy, int leftW) {
  int right = 308;
  if (v.clock && timeValid()) right -= g.text(fmtClock(time(nullptr), v.h24), 308, cy, INTER_11, v.th->text, AL_R) + 10;
  int w = statusSeg(g, v, right, cy, right - 12 - leftW - 10);
  return w ? right - w - 10 : right;
}

static void header(Gfx& g, const View& v, const char* left) {
  int right = headerRight(g, v, 14, g.width(left, INTER_11));
  g.text(left, 12, 14, INTER_11, v.th->sub, AL_L, right - 12);
}

static void summaryHeader(Gfx& g, const View& v) {
  g.text("My Teams", 12, 18, INTER_17, v.th->text);
  headerRight(g, v, 18, g.width("My Teams", INTER_17));
}

void redrawHeader(Gfx& g) {
  if (!lastHdr.valid) return;
  const View& v = lastHdr.v;
  g.region(v.th->bg, 0, 0, W, lastHdr.summary ? 33 : 27, [&] {
    if (lastHdr.summary) summaryHeader(g, v);
    else header(g, v, lastHdr.left.c_str());
  });
}

static void footer(Gfx& g, const View& v, int y, int h, int page, int pages) {
  int cy = y + h / 2;
  if (pages > 1) {
    int total = (pages - 1) * 6;
    for (int i = 0; i < pages; i++) total += i == page ? 14 : 5;
    int x = W / 2 - total / 2;
    for (int i = 0; i < pages; i++) {
      int w = i == page ? 14 : 5;
      g.rrect(x, cy - 2, w, 5, 2, i == page ? v.accent : v.th->dot);
      x += w + 6;
    }
  }
  if (demoTag) g.text(demoTag, 12, cy, INTER_11, v.th->sub, AL_L);
  // settings gear: tapping the bottom-right corner opens the info screen with the phone QR code
  int gx = 300;
  for (int i = 0; i < 8; i++) g.arc(gx, cy, 7, 4, i * 45 - 12, i * 45 + 12, v.th->sub);
  g.circle(gx, cy, 5, v.th->sub);
  g.circle(gx, cy, 2, v.th->bg);
}

static void football(Gfx& g, int cx, int cy, uint32_t c, uint32_t lace) {
  g.ellipse(cx, cy, 6, 4, c);
  g.rect(cx - 3, cy, 6, 1, lace);
  for (int i = -1; i <= 1; i++) g.rect(cx + i * 2, cy - 1, 1, 3, lace);
}

static void card(Gfx& g, const View& v, int y, int h) { g.rrect(10, y, 300, h, 12, v.th->surface); }

static void heroTeam(Gfx& g, const View& v, const Side& s, const char* record, int colX, int top, bool fav,
                     bool poss, bool dim, bool showTO) {
  const int L = v.logo, cx = colX + COLW / 2;
  g.logo(s.logo, L, cx - L / 2, top, v.dark, teamColor(s, v.dark), s.abbr);
  int ay = top + L + 4 + 10;
  // team name ("Broncos"), stepping the font down so long ones ("Golden Knights") still fit
  const char* label = s.name[0] ? s.name : s.abbr;
  const int maxw = NAMEW - (poss ? 17 : 0);
  const uint8_t* fnt = INTER_15;
  for (const uint8_t* f : {INTER_15, INTER_13B, INTER_12}) {
    fnt = f;
    if (g.width(label, f) <= maxw) break;
  }
  int aw = min(g.width(label, fnt), maxw);
  int x = cx - (aw + (poss ? 17 : 0)) / 2;
  if (poss) {
    football(g, x + 6, ay, v.accent, v.th->bg);
    x += 17;
  }
  g.text(label, x, ay, fnt, dim ? v.th->dim : v.th->text, AL_L, maxw);
  int ry = top + L + 4 + 20 + 7;
  bool to = showTO && s.timeouts >= 0;
  if (v.records && record && *record) {
    int rw = g.width(record, INTER_11);
    int total = rw + (to ? 28 : 0);
    int rx = cx - total / 2;
    g.text(record, rx, ry, INTER_11, v.th->sub, AL_L, COLW - 4);
    if (to) {
      for (int i = 0; i < 3; i++) g.rect(rx + rw + 6 + i * 8, ry - 1, 6, 3, i < s.timeouts ? v.th->text : v.th->dot);
    }
  } else if (to) {
    for (int i = 0; i < 3; i++) g.rect(cx - 11 + i * 8, ry - 1, 6, 3, i < s.timeouts ? v.th->text : v.th->dot);
  }
  if (fav) g.rrect(cx - 8, top + L + 4 + 20 + 14 + 4, 16, 3, 1, v.accent);
}

// yard line (0..100 from the home goal) -> x on a field drawn at fx with 30px end zones and 216px of field
static int yardX(int fx, int yl) { return fx + 30 + (100 - yl) * 216 / 100; }

static String yardText(const Game& gm, int yl) {
  if (yl == 50) return "50";
  if (yl < 50) return String(gm.home.abbr) + " " + yl;
  return String(gm.away.abbr) + " " + (100 - yl);
}

static void fieldCard(Gfx& g, const View& v, const FavTeam& t, int y) {
  const Game& gm = t.game;
  const Theme& th = *v.th;
  card(g, v, y, 84);
  bool possHome = strcmp(gm.possession, gm.home.id) == 0;
  bool possAway = strcmp(gm.possession, gm.away.id) == 0;

  // row 1: down & distance | win probability / red zone
  int r1 = y + 14, right = 298;
  if (gm.redZone) {
    int w = g.width("RED ZONE", INTER_11) + 12;
    g.rrect(right - w, r1 - 8, w, 16, 8, th.live);
    g.text("RED ZONE", right - w / 2, r1, INTER_11, 0xFFFFFFu, AL_C);
    right -= w + 8;
  } else if (gm.homeWinPct >= 0) {
    const Side* f = t.favSide();
    const Side* who = f ? f : &gm.home;
    int pct = (who == &gm.home) ? gm.homeWinPct : 100 - gm.homeWinPct;
    String val = String(who->abbr) + " " + pct + "%";
    int vw = g.text(val, right, r1, INTER_11, th.text, AL_R);
    int lw = g.text("Win ", right - vw, r1, INTER_11, th.sub, AL_R);
    right -= vw + lw + 8;
  }
  String lead = gm.downText[0] ? String(gm.downText) : String(possHome ? gm.home.abbr : possAway ? gm.away.abbr : "") + (possHome || possAway ? " ball" : "");
  int lw = g.text(lead, 22, r1, INTER_13B, th.text, AL_L, right - 22);
  if (gm.possText[0] && gm.downText[0]) g.text(String("at ") + gm.possText, 22 + lw + 5, r1, INTER_13, th.sub, AL_L, right - 27 - lw);

  // field
  const int fx = 22, fy = y + 24, fw = 276, fh = 34;
  uint32_t awayC = teamColor(gm.away, true), homeC = teamColor(gm.home, true);
  g.rrect(fx, fy, fw, fh, 6, FIELD);
  g.rrect(fx, fy, 36, fh, 6, gm.away.color);
  g.rrect(fx + fw - 36, fy, 36, fh, 6, gm.home.color);
  g.rect(fx + 30, fy, 6, fh, FIELD);
  g.rect(fx + fw - 36, fy, 6, fh, FIELD);
  g.text(gm.away.abbr, fx + 15, fy + fh / 2, INTER_11, lum(gm.away.color) < 150 ? (lum(awayC) > 90 && awayC != gm.away.color ? awayC : 0xFFFFFFu) : 0x111113u, AL_C, 29);
  g.text(gm.home.abbr, fx + fw - 15, fy + fh / 2, INTER_11, lum(gm.home.color) < 150 ? (lum(homeC) > 90 && homeC != gm.home.color ? homeC : 0xFFFFFFu) : 0x111113u, AL_C, 29);
  for (int k = 1; k <= 9; k++) {
    int x = fx + 30 + k * 216 / 10;
    g.rect(x, fy, 1, fh, mix(0xFFFFFFu, FIELD, k == 5 ? 0.55f : 0.22f));
  }
  if (gm.yardLine >= 0 && gm.yardLine <= 100) {
    int los = yardX(fx, gm.yardLine);
    if (gm.driveStart > 0 && gm.driveStart < 100) {
      int ds = yardX(fx, gm.driveStart);
      g.rect(min(ds, los), fy, abs(ds - los), fh, mix(v.accent, FIELD, 0.4f));
    }
    if (gm.down > 0 && gm.distance > 0 && (possHome || possAway)) {
      int fd = constrain(possHome ? gm.yardLine + gm.distance : gm.yardLine - gm.distance, 0, 100);
      g.rect(yardX(fx, fd) - 1, fy, 2, fh, FIRST_DOWN);
    }
    g.rect(los - 1, fy, 2, fh, SCRIMMAGE);
    g.ellipse(los, fy + fh / 2, 8, 6, 0xFFFFFFu);
    g.ellipse(los, fy + fh / 2, 7, 5, 0x8A4B22u);
    g.rect(los - 3, fy + fh / 2, 6, 1, 0xFFFFFFu);
    for (int i = -1; i <= 1; i++) g.rect(los + i * 2, fy + fh / 2 - 1, 1, 3, 0xFFFFFFu);
  }

  // row 3: drive summary | where it started
  int r3 = y + 70, r3right = 298;
  if (gm.driveStart > 0 && gm.driveStart < 100) {
    r3right -= g.text(String("from ") + yardText(gm, gm.driveStart), 298, r3, INTER_11, th.sub, AL_R) + 8;
  }
  if (gm.driveDesc[0]) {
    String d = gm.driveDesc;
    d.replace(" yards", " yds");
    d.replace(" yard", " yd");
    d.replace(", ", " \xC2\xB7 ");
    g.text(String("Drive: ") + d, 22, r3, INTER_11, th.sub, AL_L, r3right - 22);
  }
}

static void bases(Gfx& g, const View& v, int cx, int cy, const Game& gm) {
  auto base = [&](int x, int y, bool on) {
    const int r = 7;
    uint32_t c = on ? v.accent : v.th->dot;
    g.tri(x, y - r, x + r, y, x, y + r, c);
    g.tri(x, y - r, x - r, y, x, y + r, c);
  };
  base(cx + 11, cy + 3, gm.on1);
  base(cx, cy - 8, gm.on2);
  base(cx - 11, cy + 3, gm.on3);
}

static void baseballCard(Gfx& g, const View& v, const FavTeam& t, int y) {
  const Game& gm = t.game;
  card(g, v, y, 52);
  char cnt[12];
  snprintf(cnt, sizeof cnt, "%d-%d", gm.balls, gm.strikes);
  g.text(cnt, 22, y + 19, INTER_17, v.th->text);
  char outs[12];
  snprintf(outs, sizeof outs, "%d out%s", gm.outs, gm.outs == 1 ? "" : "s");
  g.text(outs, 22, y + 37, INTER_11, v.th->sub);
  if (gm.lastPlay[0]) g.text(gm.lastPlay, 80, y + 26, INTER_12, v.th->sub, AL_L, 170);
  bases(g, v, 276, y + 26, gm);
}

static void textCard(Gfx& g, const View& v, int y, const char* label, const char* body) {
  card(g, v, y, 52);
  g.text(label, 22, y + 17, INTER_11, v.th->sub);
  g.text(body, 22, y + 35, INTER_13, v.th->text, AL_L, 276);
}

static void pinIcon(Gfx& g, int x, int cy, uint32_t c, uint32_t bg) {
  g.circle(x + 8, cy - 2, 5, c);
  g.tri(x + 4, cy, x + 12, cy, x + 8, cy + 7, c);
  g.circle(x + 8, cy - 2, 2, bg);
}

// pre-game: where it's being played (home/away, city, stadium)
static void preCard(Gfx& g, const View& v, const FavTeam& t, int y) {
  const Game& gm = t.game;
  if (!gm.venue[0]) return;
  card(g, v, y, 52);
  const Theme& th = *v.th;
  int x = 22;
  pinIcon(g, x, y + 26, th.sub, th.surface);
  String label = t.id == gm.home.id ? "Home game" : (t.id == gm.away.id ? "Away game" : "Venue");
  if (gm.venueCity[0]) label += String("  \xC2\xB7  ") + gm.venueCity;
  g.text(label, x + 24, y + 18, INTER_11, th.sub, AL_L, 298 - x - 24);
  g.text(gm.venue, x + 24, y + 35, INTER_15, th.text, AL_L, 298 - x - 24);
}

static void finalCard(Gfx& g, const View& v, const FavTeam& t, int y) {
  const Game& gm = t.game;
  const Theme& th = *v.th;
  const Side* f = t.favSide();
  const Side* o = t.oppSide();
  int np = max(gm.away.nls, gm.home.nls);
  bool table = v.linescore && np > 0;
  bool haveResult = f && o && allDigits(f->score) && allDigits(o->score);
  if (!table && !haveResult) return;
  card(g, v, y, 52);

  int x = 22, colW = np <= 5 ? 22 : 16;
  int tableW = 36 + np * colW + 26;
  if (table) {
    auto label = [&](int i) -> String {
      if (gm.sport == SP_HOCKEY && i >= 3) return i == 3 ? "OT" : "SO";
      if ((gm.sport == SP_FOOTBALL || gm.sport == SP_BASKETBALL) && i >= 4) return i == 4 ? "OT" : String(i - 3) + "OT";
      return String(i + 1);
    };
    int rows[3] = {y + 13, y + 27, y + 41};
    for (int i = 0; i < np; i++) g.text(label(i), x + 36 + i * colW + colW / 2, rows[0], INTER_11, th.dim, AL_C);
    g.text(gm.sport == SP_BASEBALL ? "R" : "T", x + 36 + np * colW + 13, rows[0], INTER_11, th.dim, AL_C);
    const Side* sides[2] = {&gm.away, &gm.home};
    int as = atoi(gm.away.score), hs = atoi(gm.home.score);
    for (int r = 0; r < 2; r++) {
      const Side& s = *sides[r];
      bool lost = (r == 0 ? as < hs : hs < as);
      uint32_t c = lost ? th.sub : th.text;
      g.text(s.abbr, x, rows[r + 1], INTER_11, c, AL_L, 34);
      for (int i = 0; i < s.nls; i++) g.text(String(s.ls[i]), x + 36 + i * colW + colW / 2, rows[r + 1], INTER_11, c, AL_C);
      g.text(s.score, x + 36 + np * colW + 13, rows[r + 1], INTER_13B, c, AL_C);
    }
    x += tableW + 12;
  }
  if (haveResult && 298 - x >= 84) {
    int a = atoi(f->score), b = atoi(o->score);
    const char* res = a > b ? "W" : (a < b ? "L" : "T");
    uint32_t bc = a > b ? th.win : (a < b ? th.live : th.dim);
    g.rrect(x, y + 15, 22, 22, 6, bc);
    g.text(res, x + 11, y + 26, INTER_13B, 0xFFFFFFu, AL_C);
    String name = f->name[0] ? f->name : f->abbr;
    g.text(name + (a > b ? " win" : (a < b ? " lose" : " tie")), x + 30, y + 20, INTER_12, th.text, AL_L, 298 - x - 30);
    if (t.record[0]) g.text(String("Now ") + t.record, x + 30, y + 34, INTER_11, th.sub, AL_L, 298 - x - 30);
  }
}

// ------------------------------------------------------------------ team card
void drawCard(Gfx& g, const View& v, const FavTeam& t, int page, int pages, const Banner* banner) {
  const Theme& th = *v.th;
  const Game& gm = t.game;
  liveDot.on = false;

  bool live = t.isLive(), pre = gm.valid && strcmp(gm.state, "pre") == 0, post = gm.valid && strcmp(gm.state, "post") == 0;
  bool footballField = live && v.situation && gm.sport == SP_FOOTBALL && gm.yardLine >= 0;
  bool baseball = live && v.situation && gm.sport == SP_BASEBALL && gm.outs >= 0;
  bool lastPlay = live && v.situation && !footballField && !baseball && gm.lastPlay[0];
  bool preInfo = pre && gm.venue[0];
  bool postInfo = post && ((v.linescore && max(gm.home.nls, gm.away.nls) > 0) || t.favSide());
  int cardH = footballField ? 84 : ((baseball || lastPlay || preInfo || postInfo) ? 52 : 0);
  int footH = cardH == 84 ? 20 : 28;
  int cardY = 240 - footH - cardH;
  int heroTop = 28, heroBot = cardH ? cardY - 4 : 240 - footH;
  int heroH = v.logo + 45;
  int top = heroTop + max(0, (heroBot - heroTop - heroH) / 2);

  // header text: league + something useful
  String left = t.label;
  if (live && gm.sport == SP_FOOTBALL && gm.redZone) left += " \xC2\xB7 RED ZONE";

  // records: the favorite's season record from the team endpoint is the freshest
  Side away = gm.away, home = gm.home;
  if (t.record[0]) {
    if (t.id == away.id) strlcpy(away.record, t.record, sizeof away.record);
    if (t.id == home.id) strlcpy(home.record, t.record, sizeof home.record);
  }

  // pre-compute live dot position for the animation
  int as = atoi(gm.away.score), hs = atoi(gm.home.score);

  lastHdr.valid = !(banner && banner->on);
  lastHdr.summary = false;
  lastHdr.left = left;
  lastHdr.v = v;
  g.frame(th.bg, [&] {
    if (banner && banner->on) {
      g.rect(0, 0, W, 28, banner->color);
      g.text(banner->text, W / 2, 14, INTER_13B, readableOn(banner->color), AL_C, 296);
    } else {
      header(g, v, left.c_str());
    }

    if (!gm.valid) {
      Side me;
      strlcpy(me.abbr, t.abbr.c_str(), sizeof me.abbr);
      strlcpy(me.logo, t.logo.c_str(), sizeof me.logo);
      strlcpy(me.name, t.name.c_str(), sizeof me.name);
      me.color = me.alt = t.color;
      int L = v.logo;
      int y0 = 28 + (240 - 28 - 28 - (L + 70)) / 2;
      g.logo(me.logo, L, W / 2 - L / 2, y0, v.dark, teamColor(me, v.dark), me.abbr);
      g.text(t.name, W / 2, y0 + L + 16, INTER_17, th.text, AL_C, 296);
      g.text(t.noUpcoming ? "No upcoming games" : "Loading\xE2\x80\xA6", W / 2, y0 + L + 38, INTER_12, th.sub, AL_C);
      if (t.record[0] && v.records) g.text(String("Record ") + t.record, W / 2, y0 + L + 56, INTER_11, th.sub, AL_C);
      footer(g, v, 212, 28, page, pages);
      return;
    }

    bool favAway = t.id == gm.away.id, favHome = t.id == gm.home.id;
    bool possA = live && strcmp(gm.possession, gm.away.id) == 0 && gm.sport == SP_FOOTBALL;
    bool possH = live && strcmp(gm.possession, gm.home.id) == 0 && gm.sport == SP_FOOTBALL;
    heroTeam(g, v, away, away.record, 8, top, favAway, possA, post && as < hs, live && gm.sport == SP_FOOTBALL);
    heroTeam(g, v, home, home.record, W - 8 - COLW, top, favHome, possH, post && hs < as, live && gm.sport == SP_FOOTBALL);

    // center column
    const int cx0 = 8 + COLW, cx1 = W - 8 - COLW, ccx = (cx0 + cx1) / 2;
    int scy = top + v.logo / 2 + 2;
    if (pre) {
      String hm, ap;
      splitClock(gm.start, v.h24, hm, ap);
      g.text(fmtDay(gm.start), ccx, scy - 22, INTER_11, th.sub, AL_C, cx1 - cx0);
      int hw = g.width(hm, INTER_30), aw = ap.length() ? g.width(ap, INTER_13) + 3 : 0;
      int x = ccx - (hw + aw) / 2;
      g.text(hm, x, scy, INTER_30, th.text);
      if (aw) g.text(ap, x + hw + 3, scy + 4, INTER_13, th.sub);
      if (v.countdown) {
        String cd = fmtCountdown(gm.start);
        int w = g.width(cd, INTER_11) + 18;
        g.rrect(ccx - w / 2, scy + 18, w, 18, 9, mix(v.accent, th.bg, 0.18f));
        g.text(cd, ccx, scy + 27, INTER_11, v.accent, AL_C);
      }
    } else {
      bool big = strlen(gm.away.score) <= 2 && strlen(gm.home.score) <= 2;
      const uint8_t* sf = big ? INTER_40 : INTER_30;
      bool awayLead = as > hs, homeLead = hs > as;
      uint32_t ac = (live || post) && homeLead ? th.dim : th.text;
      uint32_t hc = (live || post) && awayLead ? th.dim : th.text;
      int aw = g.text(gm.away.score, cx0 + 4, scy, sf, ac, AL_L);
      int hw = g.text(gm.home.score, cx1 - 4, scy, sf, hc, AL_R);
      int midL = cx0 + 4 + aw + 4, midR = cx1 - 4 - hw - 4;
      if (post) {
        String fin = upper(gm.detail);
        int fw = g.width(fin, INTER_11);
        if (fw <= midR - midL) g.text(fin, ccx, scy, INTER_11, th.sub, AL_C);
        else g.text(fin, ccx, scy + 30, INTER_11, th.sub, AL_C, cx1 - cx0);
        // winner arrow
        if (awayLead) g.tri(cx0 + 4 + aw + 9, scy - 5, cx0 + 4 + aw + 9, scy + 5, cx0 + 4 + aw + 3, scy, th.text);
        if (homeLead) g.tri(cx1 - 4 - hw - 9, scy - 5, cx1 - 4 - hw - 9, scy + 5, cx1 - 4 - hw - 3, scy, th.text);
      } else {
        // live: "2:14 - 4th" -> period on top (red), clock below
        String d = gm.detail, per = d, clk = "";
        int dash = d.indexOf(" - ");
        if (dash > 0) {
          clk = d.substring(0, dash);
          per = d.substring(dash + 3);
          if (gm.sport == SP_FOOTBALL || gm.sport == SP_BASKETBALL) {
            if (per == "1st") per = "Q1";
            else if (per == "2nd") per = "Q2";
            else if (per == "3rd") per = "Q3";
            else if (per == "4th") per = "Q4";
          } else if (gm.sport == SP_HOCKEY) {
            if (per == "1st") per = "P1";
            else if (per == "2nd") per = "P2";
            else if (per == "3rd") per = "P3";
          }
        }
        int pw = g.width(per, INTER_11) + 10, cw = clk.length() ? g.width(clk, INTER_13) : 0;
        bool inline_ = max(pw, cw) <= midR - midL;
        int py = inline_ ? (clk.length() ? scy - 8 : scy) : scy + 28;
        int px = ccx - pw / 2;
        if (!inline_ && clk.length()) {  // one line under the scores: "● Q4 2:14"
          int tot = pw + 6 + cw;
          px = ccx - tot / 2;
          g.text(clk, px + pw + 6, py, INTER_13, th.text);
        } else if (clk.length()) {
          g.text(clk, ccx, scy + 9, INTER_13, th.text, AL_C);
        }
        g.circle(px + 3, py, 3, th.live);
        g.text(per, px + 10, py, INTER_11, th.live, AL_L, cx1 - cx0);
        liveDot.on = true;
        liveDot.x = px + 3;
        liveDot.y = py;
        liveDot.bg = th.bg;
        liveDot.color = th.live;
      }
    }

    if (footballField) fieldCard(g, v, t, cardY);
    else if (baseball) baseballCard(g, v, t, cardY);
    else if (lastPlay) textCard(g, v, cardY, "Last play", gm.lastPlay);
    else if (preInfo) preCard(g, v, t, cardY);
    else if (postInfo) finalCard(g, v, t, cardY);

    footer(g, v, 240 - footH, footH, page, pages);
  });
}

// ------------------------------------------------------------------ summary
static int summaryRowH(int n) { return n <= 3 ? 56 : 44; }
static constexpr int SUMMARY_Y = 36;

int summaryRowAt(int n, int y) {
  int rh = summaryRowH(n);
  // rows are full width; accept touches up to 6 px outside the list (resistive panels drift)
  if (y < SUMMARY_Y - 6 || y > SUMMARY_Y + n * (rh + 1) + 6) return -1;
  return constrain((y - SUMMARY_Y) / (rh + 1), 0, n - 1);
}

void drawSummary(Gfx& g, const View& v, const std::vector<FavTeam>& list, int first, int page, int pages) {
  const Theme& th = *v.th;
  int n = min(4, (int)list.size() - first);
  int rh = summaryRowH(n);
  liveDot.on = false;
  lastHdr.valid = true;
  lastHdr.summary = true;
  lastHdr.v = v;
  g.frame(th.bg, [&] {
    summaryHeader(g, v);
    int y = 36;
    g.rrect(10, y, 300, n * rh + (n - 1), 12, th.surface);
    for (int i = 0; i < n; i++) {
      const FavTeam& t = list[first + i];
      const Game& gm = t.game;
      const Side* f = t.favSide();
      const Side* o = t.oppSide();
      int ry = y + i * (rh + 1), cy = ry + rh / 2;
      if (i > 0) g.rect(64, ry - 1, 234, 1, th.line);
      Side me;
      if (f) me = *f;
      else {
        strlcpy(me.abbr, t.abbr.c_str(), sizeof me.abbr);
        strlcpy(me.logo, t.logo.c_str(), sizeof me.logo);
        me.color = me.alt = t.color;
      }
      g.logo(me.logo, 32, 22, cy - 16, v.dark, teamColor(me, v.dark), me.abbr);
      String name = f && f->name[0] ? String(f->name) : t.name.substring(t.name.lastIndexOf(' ') + 1);

      // right column
      int rightW = 0;
      String sub;
      uint32_t subC = th.sub;
      bool liveRow = false;
      if (gm.valid && f && o) {
        bool home = f == &gm.home;
        String opp = String(home ? "vs " : "@ ") + (o->name[0] ? o->name : o->abbr);
        if (strcmp(gm.state, "pre") == 0) {
          String tm = fmtClock(gm.start, v.h24);
          String day = fmtDay(gm.start);
          bool today = day == "TODAY" || day == "TONIGHT";
          rightW = g.text(today ? tm : day.substring(0, 3) + " " + tm, 298, cy - 7, INTER_15, th.text, AL_R);
          if (v.countdown) rightW = max(rightW, g.text(fmtCountdown(gm.start), 298, cy + 9, INTER_11, v.accent, AL_R));
          sub = opp + (v.tv && gm.broadcast[0] ? String(" \xC2\xB7 ") + gm.broadcast : String(""));
        } else {
          int fs = atoi(f->score), os = atoi(o->score);
          String sc = String(f->score);
          String sc2 = String(" \xE2\x80\x93 ") + o->score;
          int w2 = g.text(sc2, 298, cy, INTER_17, th.dim, AL_R);
          int w1 = g.text(sc, 298 - w2, cy, INTER_17, th.text, AL_R);
          rightW = w1 + w2;
          if (strcmp(gm.state, "post") == 0) {
            const char* r = fs > os ? "W" : (fs < os ? "L" : "T");
            uint32_t bc = fs > os ? th.win : (fs < os ? th.live : th.dim);
            int bx = 298 - rightW - 24;
            g.rrect(bx, cy - 9, 18, 18, 5, bc);
            g.text(r, bx + 9, cy, INTER_11, 0xFFFFFFu, AL_C);
            rightW += 24;
            sub = String("Final \xC2\xB7 ") + opp;
          } else {
            liveRow = true;
            String d = gm.detail;
            d.replace(" - ", " ");
            sub = d + " \xC2\xB7 " + opp;
            subC = th.live;
          }
        }
      } else {
        sub = t.noUpcoming ? "No upcoming games" : "Loading\xE2\x80\xA6";
      }
      int textW = 298 - rightW - 10 - 64;
      g.text(name, 64, cy - 8, INTER_15, th.text, AL_L, textW);
      int sx = 64;
      if (liveRow) {
        g.circle(67, cy + 9, 3, th.live);
        sx = 74;
      }
      g.text(sub, sx, cy + 9, INTER_11, subC, AL_L, textW - (sx - 64));
    }
    footer(g, v, 212, 28, page, pages);
  });
}

// ------------------------------------------------------------------ celebration
void drawCelebrate(Gfx& g, const View& v, const FavTeam& t, const char* word, float t01) {
  const Side* f = t.favSide();
  const Side* o = t.oppSide();
  uint32_t bg = f ? f->color : t.color;
  uint32_t hi = f ? f->alt : 0xFFFFFFu;
  int contrast = abs((int)lum(hi) - (int)lum(bg));
  if (contrast < 70) hi = readableOn(bg);
  uint32_t txt = readableOn(bg);
  uint32_t dimTxt = mix(txt, bg, 0.65f);
  g.frame(bg, [&] {
    for (int i = 0; i < 3; i++) {
      int r = 50 + i * 26 + (int)(t01 * 26);
      float a = 0.45f - i * 0.12f - t01 * 0.15f;
      if (a > 0.03f) g.ring(160, 70, r, 2, mix(hi, bg, a));
    }
    if (f) g.logo(f->logo, 80, 120, 30, lum(bg) < 120, hi, f->abbr);
    // headline in Anton (heavy condensed scoreboard face) with a hard drop shadow;
    // it rises into place over the first ~quarter of the animation
    float in = constrain(t01 / 0.25f, 0.0f, 1.0f);
    int wy = 136 + (int)((1 - in) * (1 - in) * 14);
    g.text(word, 160, wy + 3, ANTON_44, mix(0x000000u, bg, 0.35f), AL_C, 300);
    g.text(word, 160, wy, ANTON_44, hi, AL_C, 300);
    if (f && o) {
      String a = String(f->abbr) + "  ";
      String sa = f->score, sb = o->score;
      String b = String("  ") + o->abbr;
      int wa = g.width(a, INTER_15), wsa = g.width(sa, INTER_17), wd = g.width(" \xE2\x80\x93 ", INTER_15),
          wsb = g.width(sb, INTER_17), wb = g.width(b, INTER_15);
      int x = 160 - (wa + wsa + wd + wsb + wb) / 2;
      x += g.text(a, x, 178, INTER_15, txt);
      x += g.text(sa, x, 178, INTER_17, txt);
      x += g.text(" \xE2\x80\x93 ", x, 178, INTER_15, dimTxt);
      x += g.text(sb, x, 178, INTER_17, dimTxt);
      g.text(b, x, 178, INTER_15, dimTxt);
      String d = t.game.detail;
      d.replace(" - ", " \xC2\xB7 ");
      g.text(d, 160, 200, INTER_11, dimTxt, AL_C, 296);
    }
  });
}

// ------------------------------------------------------------------ boot
static void check(Gfx& g, int cx, int cy, uint32_t c, uint32_t mark) {
  g.circle(cx, cy, 8, c);
  // check mark from two thick-ish strokes
  for (int d = 0; d < 2; d++) {
    g.tri(cx - 4, cy + d - 1, cx - 1, cy + 3 + d - 1, cx - 1, cy + 1 + d - 1, mark);
    g.tri(cx - 1, cy + 3 + d - 1, cx + 4, cy - 3 + d - 1, cx + 4, cy - 1 + d - 1, mark);
  }
}

void drawBoot(Gfx& g, const View& v, const BootInfo& info, uint32_t ms) {
  const Theme& th = *v.th;
  liveDot.on = false;
  float spin = fmodf(ms * 0.4f, 360.0f);
  int nTeams = info.teams.size();
  int blockH = 36 + 20 + 3 * 18 + 2 * 9 + 18 + 4 + (nTeams ? 14 + 22 : 0);
  int y0 = (240 - blockH) / 2;
  g.frame(th.bg, [&] {
    // app icon: a minimal split scoreboard (home | away) on a small base
    g.rrect(24, y0, 36, 36, 8, v.accent);
    g.rrect(31, y0 + 10, 22, 14, 3, 0xFFFFFFu);
    g.rect(41, y0 + 10, 2, 14, v.accent);
    g.rrect(38, y0 + 27, 8, 2, 1, 0xFFFFFFu);
    g.text("Sports Ticker", 70, y0 + 10, INTER_17, th.text);
    g.text("Starting up", 70, y0 + 27, INTER_11, th.sub);
    int y = y0 + 36 + 20;
    for (int i = 0; i < 3; i++) {
      int cy = y + 9;
      uint8_t s = info.state[i];
      if (s == 2) check(g, 32, cy, th.win, th.bg);
      else if (s == 3) {
        g.circle(32, cy, 8, th.live);
        g.text("!", 32, cy, INTER_13B, 0xFFFFFFu, AL_C);
      } else if (s == 1) {
        g.ring(32, cy, 8, 3, th.track);
        g.arc(32, cy, 8, 5, spin, spin + 100, v.accent);
      } else {
        g.ring(32, cy, 8, 2, th.track);
      }
      g.text(info.label[i], 50, cy, s == 1 ? INTER_13B : INTER_13, s == 0 ? th.sub : th.text, AL_L, 150);
      g.text(info.detail[i], 296, cy, INTER_11, th.sub, AL_R, 100);
      y += 18 + 9;
    }
    y += 9;
    int bw = 272;
    g.rrect(24, y, bw, 4, 2, th.track);
    int pw = (int)(bw * constrain(info.progress, 0.0f, 1.0f));
    if (pw > 3) g.rrect(24, y, pw, 4, 2, v.accent);
    // moving shimmer on the bar while working
    int sw = 70, sx = 24 + (int)((ms % 1400) / 1400.0f * (bw + sw)) - sw;
    int a = max(24, sx), b = min(24 + bw, sx + sw);
    if (b > a) g.rect(a, y, b - a, 4, mix(0xFFFFFFu, a < 24 + pw ? v.accent : th.track, 0.3f));
    y += 4 + 14;
    int x = 24;
    for (auto& tm : info.teams) {
      g.logo(tm.logo.c_str(), 22, x, y, v.dark, tm.color, tm.abbr.c_str());
      if (!tm.loaded) {  // pulse not-yet-loaded logos
        float p = 0.5f + 0.5f * sinf(ms / 190.0f);
        g.rect(x, y + 25, 22, 2, mix(v.accent, th.bg, p));
      }
      x += 30;
    }
  });
}

// ------------------------------------------------------------------ setup / info / messages
void drawSetup(Gfx& g, const View& v, const String& ip) {
  const Theme& th = *v.th;
  liveDot.on = false;
  String url = "http://" + ip + "/";
  g.frame(th.bg, [&] {
    g.text("Pick your teams", 12, 27, INTER_17, th.text);
    const char* steps[3] = {"Put your phone on the", "Scan the code, or open", "Add teams, then tap Save"};
    const char* steps2[3] = {"same Wi-Fi", nullptr, nullptr};
    int y = 52;
    for (int i = 0; i < 3; i++) {
      g.circle(21, y + 9, 9, v.accent);
      char n[2] = {char('1' + i), 0};
      g.text(n, 21, y + 9, INTER_11, readableOn(v.accent), AL_C);
      g.text(steps[i], 38, y + 8, INTER_12, th.text, AL_L, 138);
      if (steps2[i]) g.text(steps2[i], 38, y + 24, INTER_12, th.text, AL_L, 138);
      if (i == 1) g.text(ip, 38, y + 26, INTER_13B, v.accent, AL_L, 138);
      y += (i < 2) ? 44 : 30;
    }
    g.rrect(188, 56, 120, 120, 12, 0xFFFFFFu);
    g.qr(url.c_str(), 196, 64, 104);
    g.text("sportsticker.local", 248, 190, INTER_11, th.sub, AL_C, 120);
  });
}

void drawInfo(Gfx& g, const View& v, const String& ssid, int rssi, const String& ip, long ago, int bright,
              bool sliderOnly) {
  const Theme& th = *v.th;
  liveDot.on = false;
  String url = "http://" + ip + "/";
  auto draw = [&] {
    g.text("Ticker info", 12, 27, INTER_17, th.text);
    // close button: round chip with an X
    g.circle(INFO_CLOSE_CX, INFO_CLOSE_CY, INFO_CLOSE_R, th.surface);
    g.line(INFO_CLOSE_CX - 5, INFO_CLOSE_CY - 5, INFO_CLOSE_CX + 5, INFO_CLOSE_CY + 5, 1.2f, th.text);
    g.line(INFO_CLOSE_CX - 5, INFO_CLOSE_CY + 5, INFO_CLOSE_CX + 5, INFO_CLOSE_CY - 5, 1.2f, th.text);
    g.rrect(12, 48, 164, 122, 12, th.surface);
    const char* lab[3] = {"Wi-Fi", "Settings page", "Scores updated"};
    String upd = ago < 0 ? String("not yet")
                         : (lastUpdated && timeValid() ? fmtClock(lastUpdated, v.h24) + "  \xC2\xB7  " + fmtAgo(ago) : fmtAgo(ago));
    String val[3] = {ssid, ip, upd};
    for (int i = 0; i < 3; i++) {
      int ry = 48 + i * 41;
      if (i) g.rect(12, ry - 1, 164, 1, th.line);
      g.text(lab[i], 22, ry + 13, INTER_11, th.sub);
      int w = g.text(val[i], 22, ry + 29, INTER_13, i == 1 ? v.accent : th.text, AL_L, i == 0 ? 122 : 146);
      if (i == 0) {  // signal bars
        int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
        for (int b = 0; b < 4; b++) {
          int h = 3 + b * 2;
          g.rect(22 + w + 6 + b * 4, ry + 33 - h, 3, h, b < bars ? th.win : th.dot);
        }
      }
    }
    g.rrect(196, 52, 112, 112, 12, 0xFFFFFFu);
    g.qr(url.c_str(), 204, 60, 96);
    g.text("Scan to change", 252, 176, INTER_11, th.sub, AL_C);
    g.text("teams & display", 252, 190, INTER_11, th.sub, AL_C);
    // brightness slider (drag it; see INFO_SLIDER_* in screens.h)
    g.rrect(12, 178, 164, 40, 12, th.surface);
    int pct = (bright * 100 + 127) / 255;
    g.text("Brightness", 22, 190, INTER_11, th.sub);
    g.text(String(pct) + "%", 166, 190, INTER_11, th.text, AL_R);
    int kx = INFO_SLIDER_X0 + (bright - 10) * (INFO_SLIDER_X1 - INFO_SLIDER_X0) / 245;
    g.rrect(INFO_SLIDER_X0, 204, INFO_SLIDER_X1 - INFO_SLIDER_X0, 4, 2, th.track);
    g.rrect(INFO_SLIDER_X0, 204, kx - INFO_SLIDER_X0 + 2, 4, 2, v.accent);
    g.circle(kx, 206, 7, th.text);
  };
  // just the slider card: redrawing the whole bottom strip would also repaint the lower half of the
  // "Scores updated" text with a newer value than its top half, garbling it
  if (sliderOnly) g.region(th.bg, 10, 176, 168, 44, draw);
  else g.frame(th.bg, draw);
}

void drawMessage(Gfx& g, const View& v, const char* title, const char* line) {
  const Theme& th = *v.th;
  liveDot.on = false;
  g.frame(th.bg, [&] {
    g.text(title, 160, line ? 108 : 120, INTER_17, th.text, AL_C, 296);
    if (line) g.text(line, 160, 134, INTER_12, th.sub, AL_C, 296);
  });
}

void drawWifiSetup(Gfx& g, const View& v, const char* ap) {
  const Theme& th = *v.th;
  liveDot.on = false;
  String qr = String("WIFI:T:nopass;S:") + ap + ";;";
  g.frame(th.bg, [&] {
    g.text("Wi-Fi setup", 12, 27, INTER_17, th.text);
    const char* steps[3] = {"Scan the code, or join", "Pick your home Wi-Fi", "Ticker restarts itself"};
    int y = 52;
    for (int i = 0; i < 3; i++) {
      g.circle(21, y + 9, 9, v.accent);
      char n[2] = {char('1' + i), 0};
      g.text(n, 21, y + 9, INTER_11, readableOn(v.accent), AL_C);
      g.text(steps[i], 38, y + 8, INTER_12, th.text, AL_L, 138);
      if (i == 0) g.text(ap, 38, y + 26, INTER_13B, v.accent, AL_L, 140);
      if (i == 1) g.text("in the page that opens", 38, y + 24, INTER_12, th.sub, AL_L, 138);
      y += 44;
    }
    g.rrect(188, 56, 120, 120, 12, 0xFFFFFFu);
    g.qr(qr.c_str(), 196, 64, 104);
  });
}
