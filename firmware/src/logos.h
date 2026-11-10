// Team logo cache.
//   Network task: downloads a small PNG from ESPN's resizer and decodes it ONCE into raw ARGB
//                 (decoding needs ~45 KB, so it never happens while drawing or while an HTTPS
//                 connection is open). The UI task stores the pixels in a raw flash slot
//                 (logoFlush); see logos.cpp for why there's no filesystem.
//   UI:           logoDraw() streams the raw pixels row by row with alpha blending,
//                 using a few hundred bytes of RAM. Missing logos are queued for download, and
//                 the fetch task prefetches every logo a team's screens need.
#pragma once
#include <Arduino.h>
#include "lgfx_cyd.h"

void logosBegin();
// Draws the logo with its top-left at (x, y) in dst coordinates. Returns false (and queues a
// download) if it isn't cached yet. clipTop/clipBottom: dst rows outside are skipped.
bool logoDraw(LovyanGFX& dst, const char* path, int size, bool dark, int x, int y, int clipTop, int clipBottom);
void logoService();  // call from the network task: downloads + decodes into RAM (never touches flash)
bool logoFlush();    // call from the UI task: writes finished logos to flash; true if any landed
void logoPrefetch(const char* path, int size, bool dark);  // queue a download now if it isn't cached
int logoPending();                                          // downloads still queued
String themedLogoPath(const char* path, bool dark);
