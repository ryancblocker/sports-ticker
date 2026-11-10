#include "espn.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"

static const char* BASE = "https://site.api.espn.com/apis/site/v2/sports/";

static void cp(char* dst, size_t n, const char* src) { strlcpy(dst, src ? src : "", n); }

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// ESPN dates look like "2026-11-08T18:00Z" (sometimes with seconds). Always UTC.
time_t parseIsoUtc(const char* s) {
  if (!s) return 0;
  int Y, M, D, h, mi, sec = 0;
  int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &sec);
  if (n < 5) return 0;
  if (n == 5) sec = 0;
  return (time_t)(daysFromCivil(Y, M, D) * 86400 + h * 3600 + mi * 60 + sec);
}

// Streams the response straight into ArduinoJson through a filter, so a 20-300 KB
// payload only costs a few KB of RAM.
static bool getJson(const String& url, JsonDocument& filter, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();  // public, read-only sports scores; skip CA pinning
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding -> parse the raw stream
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code != 200) {
    log_w("GET %s -> %d", url.c_str(), code);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(20));
  http.end();
  if (err) log_w("JSON %s: %s", url.c_str(), err.c_str());
  return !err;
}

bool espnFetchTeam(const String& sport, const String& league, const String& id, TeamInfo& out) {
  JsonDocument filter;
  filter["team"]["record"]["items"][0]["summary"] = true;
  filter["team"]["nextEvent"][0]["id"] = true;
  filter["team"]["nextEvent"][0]["date"] = true;

  JsonDocument doc;
  if (!getJson(String(BASE) + sport + "/" + league + "/teams/" + id, filter, doc)) return false;
  JsonObject team = doc["team"];
  if (team.isNull()) return false;
  cp(out.record, sizeof out.record, team["record"]["items"][0]["summary"]);
  JsonObject ne = team["nextEvent"][0];
  cp(out.nextEventId, sizeof out.nextEventId, ne["id"]);
  out.nextDate = parseIsoUtc(ne["date"]);
  return true;
}

// Keep only the path of ESPN CDN image URLs (saves RAM; logos.cpp re-adds the host).
static void cpLogo(char* dst, size_t n, const char* url) {
  if (!url) url = "";
  const char* p = strstr(url, "espncdn.com");
  cp(dst, n, p ? p + strlen("espncdn.com") : "");
}

static void fillSide(JsonObject c, Side& s) {
  JsonObject t = c["team"];
  cp(s.id, sizeof s.id, t["id"]);
  cp(s.abbr, sizeof s.abbr, t["abbreviation"]);
  cp(s.name, sizeof s.name, t["shortDisplayName"] | t["name"].as<const char*>());
  cpLogo(s.logo, sizeof s.logo, t["logo"]);
  s.color = parseHexColor(t["color"], 0x444444);
  s.alt = parseHexColor(t["alternateColor"], 0x888888);
  JsonVariant sc = c["score"];
  const char* score = sc.is<const char*>() ? sc.as<const char*>() : sc["displayValue"].as<const char*>();
  cp(s.score, sizeof s.score, score);
  cp(s.record, sizeof s.record, c["records"][0]["summary"]);
  s.nls = 0;
  for (JsonObject l : c["linescores"].as<JsonArray>()) {
    if (s.nls >= MAX_PERIODS) break;
    s.ls[s.nls++] = (int8_t)constrain((int)(l["value"] | 0.0f), -99, 127);
  }
}

bool espnFetchEvent(const String& sport, const String& league, const char* eventId, Game& g) {
  JsonDocument filter;
  filter["id"] = true;
  filter["date"] = true;
  filter["status"]["type"]["state"] = true;
  filter["status"]["type"]["shortDetail"] = true;
  JsonObject fc = filter["competitions"][0].to<JsonObject>();
  JsonObject comp = fc["competitors"][0].to<JsonObject>();
  comp["homeAway"] = true;
  comp["score"] = true;
  comp["records"][0]["summary"] = true;
  comp["linescores"][0]["value"] = true;
  for (const char* k : {"id", "abbreviation", "shortDisplayName", "name", "color", "alternateColor", "logo"})
    comp["team"][k] = true;
  fc["broadcasts"][0]["names"] = true;
  fc["venue"]["fullName"] = true;
  fc["venue"]["address"] = true;
  JsonObject sit = fc["situation"].to<JsonObject>();
  for (const char* k : {"shortDownDistanceText", "downDistanceText", "possessionText", "possession", "isRedZone",
                        "yardLine", "down", "distance", "homeTimeouts", "awayTimeouts", "balls", "strikes", "outs",
                        "onFirst", "onSecond", "onThird"}) {
    sit[k] = true;
  }
  sit["lastPlay"]["text"] = true;
  sit["lastPlay"]["drive"]["description"] = true;
  sit["lastPlay"]["drive"]["start"]["yardLine"] = true;
  sit["lastPlay"]["probability"]["homeWinPercentage"] = true;

  JsonDocument doc;
  if (!getJson(String(BASE) + sport + "/" + league + "/scoreboard/" + eventId, filter, doc)) return false;

  Game out;
  out.sport = sportFromSlug(sport);
  cp(out.eventId, sizeof out.eventId, doc["id"] | eventId);
  out.start = parseIsoUtc(doc["date"]);
  cp(out.state, sizeof out.state, doc["status"]["type"]["state"]);
  cp(out.detail, sizeof out.detail, doc["status"]["type"]["shortDetail"]);

  JsonObject c0 = doc["competitions"][0];
  for (JsonObject c : c0["competitors"].as<JsonArray>()) {
    const char* ha = c["homeAway"] | "";
    fillSide(c, strcmp(ha, "home") == 0 ? out.home : out.away);
  }
  cp(out.broadcast, sizeof out.broadcast, c0["broadcasts"][0]["names"][0]);
  cp(out.venue, sizeof out.venue, c0["venue"]["fullName"]);
  {
    JsonObject a = c0["venue"]["address"];
    const char* city = a["city"] | "";
    const char* region = a["state"] | (a["country"] | "");
    if (*city) snprintf(out.venueCity, sizeof out.venueCity, *region ? "%s, %s" : "%s", city, region);
  }

  JsonObject s = c0["situation"];
  if (!s.isNull()) {
    cp(out.possession, sizeof out.possession, s["possession"]);
    out.redZone = s["isRedZone"] | false;
    if (s["yardLine"].is<int>()) {
      out.yardLine = (int8_t)constrain((int)s["yardLine"], -1, 100);
      out.down = s["down"] | 0;
      out.distance = s["distance"] | 0;
      cp(out.downText, sizeof out.downText, s["shortDownDistanceText"]);
      cp(out.possText, sizeof out.possText, s["possessionText"]);
      out.home.timeouts = s["homeTimeouts"] | -1;
      out.away.timeouts = s["awayTimeouts"] | -1;
      JsonObject drive = s["lastPlay"]["drive"];
      cp(out.driveDesc, sizeof out.driveDesc, drive["description"]);
      out.driveStart = drive["start"]["yardLine"].is<int>() ? (int8_t)constrain((int)drive["start"]["yardLine"], 0, 100) : -1;
    }
    JsonVariant hw = s["lastPlay"]["probability"]["homeWinPercentage"];
    if (hw.is<float>()) out.homeWinPct = (int8_t)constrain((int)lroundf(hw.as<float>() * 100), 0, 100);
    cp(out.lastPlay, sizeof out.lastPlay, s["lastPlay"]["text"]);
    if (s["outs"].is<int>()) {
      out.balls = s["balls"] | 0;
      out.strikes = s["strikes"] | 0;
      out.outs = s["outs"] | 0;
      out.on1 = s["onFirst"] | false;
      out.on2 = s["onSecond"] | false;
      out.on3 = s["onThird"] | false;
    }
  }
  out.valid = out.state[0] != 0 && out.home.abbr[0] && out.away.abbr[0];
  if (out.valid) g = out;
  return out.valid;
}
