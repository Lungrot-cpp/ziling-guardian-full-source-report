#pragma once
#include "model/yolo.h"
#include <math.h>

struct DiagnosticBox {
  int bounds[4],objectness,score,mapped;
  unsigned accepted;
};
struct DetectionDiagnostics {
  unsigned attempt,snapshot_valid,invalid_snapshots,decoded,candidates,mapped,stored;
  int selected_source;
  unsigned tracked,displayed,motion_valid,motion_cells,input_hash,input_min,input_max;
  DiagnosticBox boxes[16];
};

// Coordinates use 1/10000 of the model image; scores use 1/1000. Invalid = -2147483647.
inline int diagnostic_value(float value,float scale) {
  if(!isfinite(value))return -2147483647;
  return static_cast<int>(fmaxf(-1000000.f,fminf(1000000.f,value*scale)));
}
inline void diagnostic_decode(DetectionDiagnostics& d,const box* boxes,int count) {
  d.decoded=count;d.stored=count>16?16:count;
  for(unsigned i=0;i<d.stored;i++) {
    const box& b=boxes[i];DiagnosticBox& r=d.boxes[i];
    const float bounds[4]={b.x_min,b.y_min,b.x_max,b.y_max};
    for(unsigned k=0;k<4;k++)r.bounds[k]=diagnostic_value(bounds[k],10000);
    r.objectness=diagnostic_value(b.objectness,1000);
    r.score=diagnostic_value(b.class_probabilities[0],1000);r.mapped=-1;
  }
}
