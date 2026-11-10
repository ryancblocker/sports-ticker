#include "web.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include "app.h"
#include "config.h"
#include "web_page.h"

static WebServer server(80);

static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void handleStatus() {
  JsonDocument doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  JsonArray arr = doc["teams"].to<JsonArray>();
  {
    Lock l;
    for (const FavTeam& t : teams) {
      JsonObject o = arr.add<JsonObject>();
      o["abbr"] = t.abbr;
      o["label"] = t.label;
      o["record"] = t.record;
      const Game& g = t.game;
      if (!g.valid) {
        o["state"] = t.noUpcoming ? "none" : "loading";
        continue;
      }
      o["state"] = g.state;
      o["detail"] = g.detail;
      o["start"] = (uint32_t)g.start;
      o["away"]["abbr"] = g.away.abbr;
      o["away"]["score"] = g.away.score;
      o["home"]["abbr"] = g.home.abbr;
      o["home"]["score"] = g.home.score;
    }
  }
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void webBegin() {
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
  });
  server.on("/teams.json", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "application/json", (PGM_P)TEAMS_JSON_GZ, TEAMS_JSON_GZ_LEN);
  });
  server.on("/api/config", HTTP_GET, [] { sendJson(200, configToJson()); });
  server.on("/api/config", HTTP_POST, [] {
    String err;
    if (saveConfigJson(server.arg("plain"), err)) {
      applySettings();
      dataDirty = true;
      sendJson(200, "{\"ok\":true}");
    } else {
      JsonDocument d;
      d["ok"] = false;
      d["error"] = err;
      String out;
      serializeJson(d, out);
      sendJson(400, out);
    }
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/demo", HTTP_POST, [] {
    if (server.arg("plain").indexOf("false") >= 0) demoExitRequest = true;
    else demoRequest = true;
    sendJson(200, "{\"ok\":true}");
  });
  server.on("/api/recalibrate", HTTP_POST, [] {
    requestRecalibration();
    sendJson(200, "{\"ok\":true}");
    delay(300);
    ESP.restart();
  });
  server.on("/api/restart", HTTP_POST, [] {
    sendJson(200, "{\"ok\":true}");
    delay(300);
    ESP.restart();
  });
  server.on("/api/wifi-reset", HTTP_POST, [] {
    sendJson(200, "{\"ok\":true}");
    delay(300);
    WiFi.disconnect(true, true);  // erase saved credentials -> setup portal on boot
    delay(300);
    ESP.restart();
  });
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void webLoop() { server.handleClient(); }
