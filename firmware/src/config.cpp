#include "config.h"
#include <ArduinoJson.h>
#include <Preferences.h>

Settings settings;
std::vector<FavTeam> teams;
SemaphoreHandle_t dataMutex = nullptr;
volatile uint32_t configGen = 0;

Sport sportFromSlug(const String& s) {
  if (s == "football") return SP_FOOTBALL;
  if (s == "basketball") return SP_BASKETBALL;
  if (s == "baseball") return SP_BASEBALL;
  if (s == "hockey") return SP_HOCKEY;
  if (s == "soccer") return SP_SOCCER;
  return SP_OTHER;
}

uint32_t parseHexColor(const char* s, uint32_t def) {
  if (!s) return def;
  if (*s == '#') s++;
  if (strlen(s) < 6) return def;
  char* end = nullptr;
  uint32_t v = strtoul(s, &end, 16);
  return (end && end != s) ? (v & 0xFFFFFF) : def;
}

// Only allow characters ESPN uses in slugs/ids, so nothing odd ends up in a URL.
static bool safeSlug(const String& s) {
  if (s.isEmpty() || s.length() > 40) return false;
  for (char c : s) {
    if (!(isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_')) return false;
  }
  return true;
}

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool parseInto(const String& json, Settings& s, std::vector<FavTeam>& out, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) {
    err = e.c_str();
    return false;
  }
  s.tz = (const char*)(doc["tz"] | "EST5EDT,M3.2.0,M11.1.0");
  s.brightness = clampInt(doc["brightness"] | 200, 10, 255);
  s.nightBrightness = clampInt(doc["nightBrightness"] | 25, 0, 255);
  s.night = doc["night"] | true;
  s.nightStart = clampInt(doc["nightStart"] | 23, 0, 23);
  s.nightEnd = clampInt(doc["nightEnd"] | 7, 0, 23);
  s.theme = clampInt(doc["theme"] | 0, 0, 1);
  s.accent = String((const char*)(doc["accent"] | "team")).substring(0, 8);
  s.logoSize = clampInt(doc["logoSize"] | 2, 0, 2);
  s.showRecords = doc["showRecords"] | true;
  s.showSituation = doc["showSituation"] | true;
  s.showClock = doc["showClock"] | true;
  s.showTV = doc["showTV"] | true;
  s.showCountdown = doc["showCountdown"] | true;
  s.showLinescore = doc["showLinescore"] | true;
  s.showSummary = doc["showSummary"] | true;
  s.rotateSec = clampInt(doc["rotateSec"] | 8, 3, 120);
  s.celebrate = doc["celebrate"] | true;
  s.led = doc["led"] | true;
  s.oppAlert = doc["oppAlert"] | false;
  s.use24h = doc["use24h"] | false;
  s.invert = doc["invert"] | false;
  s.flip = doc["flip"] | false;

  out.clear();
  for (JsonObject o : doc["teams"].as<JsonArray>()) {
    if (out.size() >= MAX_TEAMS) break;
    FavTeam f;
    f.sport = (const char*)(o["sport"] | "");
    f.league = (const char*)(o["league"] | "");
    f.id = (const char*)(o["id"] | "");
    if (!safeSlug(f.sport) || !safeSlug(f.league) || !safeSlug(f.id)) continue;
    f.abbr = String((const char*)(o["abbr"] | "")).substring(0, 6);
    f.name = String((const char*)(o["name"] | "")).substring(0, 40);
    f.label = String((const char*)(o["label"] | "")).substring(0, 10);
    f.logo = String((const char*)(o["logo"] | "")).substring(0, 160);
    f.color = parseHexColor(o["color"] | "888888", 0x888888);
    out.push_back(f);
  }
  return true;
}

static String toJson(const Settings& s, const std::vector<FavTeam>& t) {
  JsonDocument doc;
  doc["tz"] = s.tz;
  doc["brightness"] = s.brightness;
  doc["nightBrightness"] = s.nightBrightness;
  doc["night"] = s.night;
  doc["nightStart"] = s.nightStart;
  doc["nightEnd"] = s.nightEnd;
  doc["theme"] = s.theme;
  doc["accent"] = s.accent;
  doc["logoSize"] = s.logoSize;
  doc["showRecords"] = s.showRecords;
  doc["showSituation"] = s.showSituation;
  doc["showClock"] = s.showClock;
  doc["showTV"] = s.showTV;
  doc["showCountdown"] = s.showCountdown;
  doc["showLinescore"] = s.showLinescore;
  doc["showSummary"] = s.showSummary;
  doc["rotateSec"] = s.rotateSec;
  doc["celebrate"] = s.celebrate;
  doc["led"] = s.led;
  doc["oppAlert"] = s.oppAlert;
  doc["use24h"] = s.use24h;
  doc["invert"] = s.invert;
  doc["flip"] = s.flip;
  JsonArray arr = doc["teams"].to<JsonArray>();
  for (const FavTeam& f : t) {
    JsonObject o = arr.add<JsonObject>();
    o["sport"] = f.sport;
    o["league"] = f.league;
    o["id"] = f.id;
    o["abbr"] = f.abbr;
    o["name"] = f.name;
    o["label"] = f.label;
    o["logo"] = f.logo;
    char hex[8];
    snprintf(hex, sizeof hex, "%06lx", (unsigned long)f.color);
    o["color"] = hex;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void configInit() {
  dataMutex = xSemaphoreCreateMutex();
  Preferences p;
  p.begin("ticker", true);
  String raw = p.getString("cfg", "");
  p.end();
  if (raw.isEmpty()) return;
  Settings s;
  std::vector<FavTeam> t;
  String err;
  if (parseInto(raw, s, t, err)) {
    settings = s;
    teams = t;
  }
}

bool saveConfigJson(const String& json, String& err) {
  Settings s;
  std::vector<FavTeam> t;
  if (!parseInto(json, s, t, err)) return false;
  String normalized = toJson(s, t);
  if (normalized.length() > 3900) {
    err = "config too large";
    return false;
  }
  Preferences p;
  p.begin("ticker", false);
  p.putString("cfg", normalized);
  p.end();
  Lock l;
  settings = s;
  teams = t;
  configGen++;
  return true;
}

String configToJson() {
  Lock l;
  return toJson(settings, teams);
}

void saveSettingsOnly() {
  String json = configToJson();
  Preferences p;
  p.begin("ticker", false);
  p.putString("cfg", json);
  p.end();
}
