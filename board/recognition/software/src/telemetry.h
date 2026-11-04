#pragma once
#include <stdint.h>
#include "event_ledger.h"
#include "sensors/sensors.h"
#include "detection_diagnostics.h"
struct TelemetryTrack {
  unsigned id,state,detector,rule,score,weighted;
  unsigned x0,y0,x1,y1,quality;
  unsigned display_x0,display_y0,display_x1,display_y1;
  unsigned ir_unconfirmed;
};
extern "C" void telemetry_service(void);
void telemetry_capture(EventRecord& event,const TelemetryTrack& track,const uint8_t* rgb);
void telemetry_publish(unsigned frame,uint32_t capture_us,uint32_t done_ms,unsigned period_ms,unsigned network_ms,
                       const TelemetryTrack* tracks,unsigned count,const EventLedger& ledger,
                       unsigned prepare_ms=0,unsigned post_ms=0,unsigned sensor_ms=0,const uint8_t* rgb=nullptr,unsigned capture_to_overlay_ms=0,
                       const DetectionDiagnostics* detection=nullptr);
