#pragma once
#include "telemetry.h"
bool tracking_begin_frame();
bool tracking_configure(const char* body);
const char* tracking_json();
void tracking_publish(const TelemetryTrack* tracks,unsigned count,unsigned frame,uint32_t capture_us);
