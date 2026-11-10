// ESPN's public (unofficial, keyless) site API.
//   Team info + next game : /apis/site/v2/sports/{sport}/{league}/teams/{id}
//   One game, live        : /apis/site/v2/sports/{sport}/{league}/scoreboard/{eventId}
#pragma once
#include <Arduino.h>
#include "model.h"

struct TeamInfo {
  char record[16] = "";
  char nextEventId[16] = "";
  time_t nextDate = 0;
};

bool espnFetchTeam(const String& sport, const String& league, const String& id, TeamInfo& out);
bool espnFetchEvent(const String& sport, const String& league, const char* eventId, Game& out);
time_t parseIsoUtc(const char* s);
