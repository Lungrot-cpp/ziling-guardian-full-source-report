#pragma once
#include "model/yolo.h"
#include "sensors/sensors.h"
#include "fusion_calibration.h"
#include <math.h>

struct FusionEvidence { bool valid; float thermal,environment; unsigned reason; };
extern bool infrared_assist_enabled,infrared_assist_active;
inline bool fusion_person_confirmed(bool enabled,const FusionEvidence& e) {
  // Unavailable thermal evidence must preserve the visual candidate.
  return !enabled || !e.valid || !isfinite(e.thermal) || e.thermal>=.5f;
}
inline float fusion_clip(float v) { return v<0 ? 0 : v>1 ? 1 : v; }
// shortcut: affine registration applies at the calibrated demo distance; use depth-aware registration for a wider working range.
inline FusionEvidence fusion_evidence(const box& b,const SensorSnapshot& s,
    bool calibrated=FUSION_CALIBRATED,const float* a=FUSION_RGB_TO_IR,
    const float* region=FUSION_RGB_REGION) {
  FusionEvidence result={false,0,0};
  result.reason=1;if(!calibrated)return result;
  result.reason=2;if(!s.valid)return result;
  result.reason=3;if(s.thermal_age_us>V6_THERMAL_AGE_US)return result;
  result.reason=4;if(s.environment_age_us>2000000)return result;
  result.reason=5;if(s.ambient_centi<0 || s.ambient_centi>4000 || s.humidity_centi<1000 || s.humidity_centi>9000)return result;
  result.reason=6;
  if(!isfinite(b.x_min)||!isfinite(b.y_min)||!isfinite(b.x_max)||!isfinite(b.y_max)||
     b.x_min>=b.x_max || b.y_min>=b.y_max || b.x_min<region[0] || b.y_min<region[1] ||
     b.x_max>region[2] || b.y_max>region[3])return result;
  result.reason=7;
  for(unsigned i=0;i<6;i++)if(!isfinite(a[i]))return result;
  const float det=a[0]*a[4]-a[1]*a[3];
  if(fabsf(det)<1e-6f)return result;
  result.reason=8;
  for(unsigned i=0;i<4;i++) {
    float x=i&1?b.x_max:b.x_min,y=i&2?b.y_max:b.y_min;
    float u=a[0]*x+a[1]*y+a[2],v=a[3]*x+a[4]*y+a[5];
    if(u<0||u>1||v<0||v>1)return result;
  }
  unsigned cells=0,good=0,warm=0;
  for(unsigned row=0;row<24;row++)for(unsigned col=0;col<32;col++) {
    float u=(col+.5f)/32-a[2],v=(row+.5f)/24-a[5];
    float x=(a[4]*u-a[1]*v)/det,y=(-a[3]*u+a[0]*v)/det;
    if(x<b.x_min||x>b.x_max||y<b.y_min||y>b.y_max)continue;
    ++cells;int t=s.heat[row*32+col];
    if(t==-32768)continue;
    ++good;
    if(t>=2700&&t<=4000&&t>=s.ambient_centi+150)++warm;
  }
  result.reason=9;
  if(cells<4 || good*5<cells*4)return result;
  result.valid=true;result.reason=0;
  // Rule scores, not learned probabilities. SHT independently measures suitability
  // of the indoor thermal contrast assumption (20..28 C and 30..70 % RH).
  result.thermal=warm<2 ? 0 : fusion_clip(4.0f*warm/good);
  if(result.thermal<.5f)result.reason=10;
  float td=fabsf(s.ambient_centi-2400.0f)-400.0f;
  float hd=fabsf(s.humidity_centi-5000.0f)-2000.0f;
  result.environment=fusion_clip(1.0f-(td>0?td:0)/1600.0f)*
                     fusion_clip(1.0f-(hd>0?hd:0)/3000.0f);
  return result;
}
inline float fusion_score(float visual,const FusionEvidence& e) {
  return .60f*fusion_clip(visual)+.25f*e.thermal+.15f*e.environment;
}
inline bool fusion_alarm(bool visual_rule,const FusionEvidence& e,float visual=1.0f) {
  return visual_rule && e.valid && isfinite(visual) && isfinite(e.thermal) && isfinite(e.environment) &&
         e.thermal>=.5f && fusion_score(visual,e)>.78f;
}
