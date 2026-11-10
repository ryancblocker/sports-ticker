// Cross-module hooks implemented in main.cpp
#pragma once
#include <Arduino.h>

extern volatile bool dataDirty;     // fetch task / web -> UI: redraw please
extern volatile bool demoRequest;   // web -> UI: start the demo of every screen
extern volatile bool demoExitRequest;
void applySettings();               // re-apply tz, brightness, rotation, ...
void requestRecalibration();        // next boot runs touch calibration
