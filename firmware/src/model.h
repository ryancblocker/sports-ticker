// Plain data shared between the fetch task, the UI and the web server.
#pragma once
#include <Arduino.h>

static constexpr size_t MAX_TEAMS = 8;
static constexpr int MAX_PERIODS = 10;

enum Sport : uint8_t { SP_OTHER, SP_FOOTBALL, SP_BASKETBALL, SP_BASEBALL, SP_HOCKEY, SP_SOCCER };
Sport sportFromSlug(const String& s);

struct Side {
  char id[8] = "";
  char abbr[8] = "";
  char name[24] = "";        // short name, e.g. "Broncos"
  char score[8] = "";
  char record[12] = "";
  char logo[80] = "";        // ESPN image path, e.g. /i/teamlogos/nfl/500/scoreboard/den.png
  uint32_t color = 0x444444;
  uint32_t alt = 0x888888;
  int8_t timeouts = -1;      // football
  int8_t nls = 0;            // number of line-score periods
  int8_t ls[MAX_PERIODS] = {};
};

struct Game {
  bool valid = false;
  Sport sport = SP_OTHER;
  char eventId[16] = "";
  char state[6] = "";        // "pre", "in", "post"
  char detail[40] = "";      // ESPN shortDetail, e.g. "2:14 - 4th", "Final/OT", "Top 5th"
  char broadcast[20] = "";
  char venue[32] = "";
  char venueCity[32] = "";   // "Kansas City, MO" / "Madrid, Spain"
  char possession[8] = "";   // team id with the ball
  bool redZone = false;
  // football
  int8_t yardLine = -1;      // 0..100, measured from the HOME team's goal line
  int8_t down = 0, distance = 0;
  int8_t driveStart = -1;    // same scale as yardLine
  char downText[16] = "";    // "3rd & 4"
  char possText[12] = "";    // "KC 32"
  char driveDesc[32] = "";   // "8 plays, 54 yards, 4:12"
  int8_t homeWinPct = -1;    // 0..100, -1 unknown
  // baseball
  int8_t balls = -1, strikes = -1, outs = -1;
  bool on1 = false, on2 = false, on3 = false;
  // everything else
  char lastPlay[72] = "";
  time_t start = 0;          // UTC epoch
  Side home, away;
};

struct FavTeam {
  // ---- configuration (from the web page) ----
  String sport, league, id, abbr, name, label, logo;
  uint32_t color = 0x888888;
  // ---- runtime (filled by the fetch task) ----
  char record[16] = "";
  bool noUpcoming = false;     // team endpoint had no nextEvent (offseason)
  bool loaded = false;         // first fetch finished (ok or not)
  Game game;
  uint32_t nextTeamFetch = 0;  // millis()
  uint32_t nextEventFetch = 0; // millis()
  time_t finishedAt = 0;       // when we saw the game go final
  int lastFavScore = -1;
  int lastOppScore = -1;

  bool isLive() const { return game.valid && strcmp(game.state, "in") == 0; }
  const Side* favSide() const {
    if (!game.valid) return nullptr;
    if (id == game.home.id) return &game.home;
    if (id == game.away.id) return &game.away;
    return nullptr;
  }
  const Side* oppSide() const {
    const Side* f = favSide();
    if (!f) return nullptr;
    return f == &game.home ? &game.away : &game.home;
  }
};
