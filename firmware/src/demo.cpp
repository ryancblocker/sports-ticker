#include "demo.h"
#include <time.h>

static Side side(const char* id, const char* abbr, const char* name, const char* logo, uint32_t color, uint32_t alt,
                 const char* score, const char* record) {
  Side s;
  strlcpy(s.id, id, sizeof s.id);
  strlcpy(s.abbr, abbr, sizeof s.abbr);
  strlcpy(s.name, name, sizeof s.name);
  strlcpy(s.logo, logo, sizeof s.logo);
  strlcpy(s.score, score, sizeof s.score);
  strlcpy(s.record, record, sizeof s.record);
  s.color = color;
  s.alt = alt;
  return s;
}

static void lines(Side& s, std::initializer_list<int> v) {
  s.nls = 0;
  for (int x : v) s.ls[s.nls++] = x;
}

static FavTeam fav(const char* sport, const char* league, const char* label, const char* id, const char* abbr,
                   const char* name, const char* logo, uint32_t color, const char* record) {
  FavTeam t;
  t.sport = sport;
  t.league = league;
  t.label = label;
  t.id = id;
  t.abbr = abbr;
  t.name = name;
  t.logo = logo;
  t.color = color;
  strlcpy(t.record, record, sizeof t.record);
  t.loaded = true;
  return t;
}

const std::vector<FavTeam>& demoTeams() {
  static std::vector<FavTeam> v;
  if (!v.empty()) return v;
  time_t now = time(nullptr);
  v.resize(DT_COUNT);

  // Broncos vs Chiefs, 4th quarter, Denver driving at the KC 32
  {
    FavTeam t = fav("football", "nfl", "NFL", "7", "DEN", "Denver Broncos", "/i/teamlogos/nfl/500/den.png", 0x0A2343, "6-3");
    Game& g = t.game;
    g.valid = true;
    g.sport = SP_FOOTBALL;
    strcpy(g.state, "in");
    strcpy(g.detail, "2:14 - 4th");
    g.away = side("12", "KC", "Chiefs", "/i/teamlogos/nfl/500/kc.png", 0xE31837, 0xFFB612, "17", "7-2");
    g.home = side("7", "DEN", "Broncos", "/i/teamlogos/nfl/500/den.png", 0x0A2343, 0xFC4C02, "21", "6-3");
    g.away.timeouts = 1;
    g.home.timeouts = 2;
    strcpy(g.possession, "7");
    g.yardLine = 68;  // KC 32 (measured from Denver's goal line)
    g.down = 3;
    g.distance = 4;
    g.driveStart = 46;
    strcpy(g.downText, "3rd & 4");
    strcpy(g.possText, "KC 32");
    strcpy(g.driveDesc, "8 plays, 54 yards, 4:12");
    g.homeWinPct = 64;
    strcpy(g.broadcast, "CBS");
    strcpy(g.venue, "Empower Field at Mile High");
    strcpy(g.venueCity, "Denver, CO");
    v[DT_BRONCOS_LIVE] = t;
  }
  // Avalanche vs Golden Knights tonight
  {
    FavTeam t = fav("hockey", "nhl", "NHL", "17", "COL", "Colorado Avalanche", "/i/teamlogos/nhl/500/col.png", 0x860038, "10-3-1");
    Game& g = t.game;
    g.valid = true;
    g.sport = SP_HOCKEY;
    strcpy(g.state, "pre");
    g.start = now + 2 * 3600 + 18 * 60;
    g.away = side("37", "VGK", "Golden Knights", "/i/teamlogos/nhl/500/vgk.png", 0x344043, 0xB4975A, "0", "9-4-1");
    g.home = side("17", "COL", "Avalanche", "/i/teamlogos/nhl/500/col.png", 0x860038, 0x005EA3, "0", "10-3-1");
    strcpy(g.broadcast, "Altitude");
    strcpy(g.venue, "Ball Arena");
    strcpy(g.venueCity, "Denver, CO");
    v[DT_AVS_UPCOMING] = t;
  }
  // Colorado State beat Boise State
  {
    FavTeam t = fav("football", "college-football", "NCAAF", "36", "CSU", "Colorado State Rams", "/i/teamlogos/ncaa/500/36.png", 0x004C23, "7-2");
    Game& g = t.game;
    g.valid = true;
    g.sport = SP_FOOTBALL;
    strcpy(g.state, "post");
    strcpy(g.detail, "Final");
    g.away = side("68", "BOIS", "Broncos", "/i/teamlogos/ncaa/500/68.png", 0x0033A0, 0xD64309, "24", "6-3");
    g.home = side("36", "CSU", "Rams", "/i/teamlogos/ncaa/500/36.png", 0x004C23, 0xC8C372, "31", "7-2");
    lines(g.away, {7, 3, 7, 7});
    lines(g.home, {3, 14, 7, 7});
    v[DT_RAMS_FINAL] = t;
  }
  // Avalanche live, 2nd period
  {
    FavTeam t = fav("hockey", "nhl", "NHL", "17", "COL", "Colorado Avalanche", "/i/teamlogos/nhl/500/col.png", 0x860038, "10-3-1");
    Game& g = t.game;
    g.valid = true;
    g.sport = SP_HOCKEY;
    strcpy(g.state, "in");
    strcpy(g.detail, "8:41 - 2nd");
    g.away = side("37", "VGK", "Golden Knights", "/i/teamlogos/nhl/500/vgk.png", 0x344043, 0xB4975A, "1", "9-4-1");
    g.home = side("17", "COL", "Avalanche", "/i/teamlogos/nhl/500/col.png", 0x860038, 0x005EA3, "2", "10-3-1");
    strcpy(g.lastPlay, "Goal: Nathan MacKinnon (8) wrist shot, assisted by Cale Makar");
    v[DT_AVS_LIVE] = t;
  }
  // Nuggets live, three-digit scores (smaller score font)
  {
    FavTeam t = fav("basketball", "nba", "NBA", "7", "DEN", "Denver Nuggets", "/i/teamlogos/nba/500/den.png", 0x0E2240, "6-3");
    Game& g = t.game;
    g.valid = true;
    g.sport = SP_BASKETBALL;
    strcpy(g.state, "in");
    strcpy(g.detail, "1:52 - 4th");
    g.away = side("13", "LAL", "Lakers", "/i/teamlogos/nba/500/lal.png", 0x552583, 0xFDB927, "108", "5-4");
    g.home = side("7", "DEN", "Nuggets", "/i/teamlogos/nba/500/den.png", 0x0E2240, 0xFEC524, "112", "6-3");
    strcpy(g.lastPlay, "Nikola Jokic makes 18-foot step back jumpshot");
    v[DT_NUGGETS_LIVE] = t;
  }
  // Rockies live, runners on 1st and 3rd
  {
    FavTeam t = fav("baseball", "mlb", "MLB", "27", "COL", "Colorado Rockies", "/i/teamlogos/mlb/500/col.png", 0x33006F, "78-84");
    Game& g = t.game;
    g.valid = true;
    g.sport = SP_BASEBALL;
    strcpy(g.state, "in");
    strcpy(g.detail, "Bot 7th");
    g.away = side("19", "LAD", "Dodgers", "/i/teamlogos/mlb/500/lad.png", 0x005A9C, 0xEF3E42, "3", "93-69");
    g.home = side("27", "COL", "Rockies", "/i/teamlogos/mlb/500/col.png", 0x33006F, 0xC4CED4, "4", "78-84");
    g.balls = 2;
    g.strikes = 1;
    g.outs = 1;
    g.on1 = true;
    g.on3 = true;
    strcpy(g.lastPlay, "Tovar singles to left, Doyle to third");
    v[DT_ROCKIES_LIVE] = t;
  }
  return v;
}
