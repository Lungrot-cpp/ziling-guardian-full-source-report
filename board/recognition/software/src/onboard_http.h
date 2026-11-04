#pragma once
#ifdef __cplusplus
#include "telemetry.h"
void onboard_http_init();
bool onboard_http_active();
extern unsigned monitor_reset_requested,monitor_reset_completed;
void onboard_http_update(unsigned frame,uint32_t capture_us,uint32_t done_ms,unsigned period_ms,const TelemetryTrack* tracks,unsigned count,const EventLedger& ledger);
extern "C" {
#endif
void onboard_http_poll(void);
#ifdef __cplusplus
}
#endif
