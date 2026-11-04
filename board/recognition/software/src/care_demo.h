#pragma once
#include "telemetry.h"
void care_init();
bool care_pending();
void care_update(const TelemetryTrack* tracks,unsigned count,uint32_t capture_us);
extern "C" void care_poll();
const char* care_json();
// Form-urlencoded numeric configuration and named actions; RAM-only changes.
bool care_configure(const char* form);
bool care_action(const char* form);
