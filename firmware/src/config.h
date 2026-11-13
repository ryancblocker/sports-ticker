// User configuration: persisted as JSON in NVS, edited from the web page.
#pragma once
#include <Arduino.h>
#include <vector>
#include "model.h"

struct Settings {
  String tz = "EST5EDT,M3.2.0,M11.1.0";     // POSIX TZ string
  // brightness
  uint8_t brightness = 200;
  uint8_t nightBrightness = 25;
  bool night = true;
  int8_t nightStart = 23;
  int8_t nightEnd = 7;
  // appearance
  uint8_t theme = 0;                        // 0 dark, 1 light
  String accent = "team";                   // "team" or "rrggbb"
  uint8_t logoSize = 2;                     // 0 S(40) 1 M(48) 2 L(56)
  // show on cards
  bool showRecords = true;
  bool showSituation = true;
  bool showClock = true;
  bool showTV = true;
  bool showCountdown = true;
  bool showLinescore = true;
  // layout
  bool showSummary = true;
  uint16_t rotateSec = 8;  // 0 = off: pages change only on touch
  // alerts
  bool celebrate = true;
  bool led = true;
  bool oppAlert = false;
  // device
  bool use24h = false;
  bool invert = false;
  bool flip = false;
};

extern Settings settings;
extern std::vector<FavTeam> teams;
extern SemaphoreHandle_t dataMutex;
extern volatile uint32_t configGen;  // bumped whenever teams change

struct Lock {
  Lock() { xSemaphoreTake(dataMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(dataMutex); }
};

void configInit();                                   // creates mutex, loads NVS
bool saveConfigJson(const String& json, String& err);
String configToJson();
void saveSettingsOnly();                             // persist current settings without touching runtime team state
uint32_t parseHexColor(const char* s, uint32_t def);
