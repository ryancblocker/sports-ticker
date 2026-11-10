// Demo mode: every screen with made-up games, so the whole UI can be checked on the device.
#pragma once
#include <vector>
#include "model.h"

enum DemoScene {
  D_BOOT,
  D_LIVE_NFL,
  D_LIVE_NHL,
  D_LIVE_NBA,
  D_LIVE_MLB,
  D_UPCOMING,
  D_FINAL,
  D_SUMMARY,
  D_CELEBRATE,
  D_OPP_ALERT,
  D_SETUP,
  D_INFO,
  D_LIGHT,
  D_COUNT
};

// Index into demoTeams() for each game scene.
enum DemoTeam { DT_BRONCOS_LIVE, DT_AVS_UPCOMING, DT_RAMS_FINAL, DT_AVS_LIVE, DT_NUGGETS_LIVE, DT_ROCKIES_LIVE, DT_COUNT };

const std::vector<FavTeam>& demoTeams();  // built on first use (needs the clock for start times)
