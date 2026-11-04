#pragma once
#include "fall_state.h"
#include "camera_input.h"
#include "person_fusion.h"

// shortcut: weak new detections need calibrated thermal support; validate pose-specific pretrained weights to improve vision alone.
inline void select_person_candidates(box* boxes,int count,const FallMachine& machine,uint64_t now,uint64_t hz,const SensorSnapshot* sensors=nullptr,float strong=.78f,float weak=.60f,float threshold=.78f) {
  for(int i=0;i<count;++i) {
    box& b=boxes[i];const float score=b.class_probabilities[0];
    if(score>=strong) continue;
    bool keep=false;
    if(score>=weak) {
      box scene=b;
      scene.y_min=fall_max(0.0f,camera_y(b.y_min));
      scene.y_max=fall_min(1.0f,camera_y(b.y_max));
      if(sensors) {
        const FusionEvidence evidence=fusion_evidence(scene,*sensors);
        if(evidence.valid && evidence.thermal>=.6f && fusion_score(score,evidence)>=threshold)keep=true;
      }
      for(int j=0;j<16;++j) {
        const FallTrack& track=machine.tracks[j];
        if(track.active && track.missed<=2 && now-track.last_tick<=hz*FALL_TRACK_GAP_TENTHS/10 && fall_iou(track.box,scene)>.15f) keep=true;
      }
      const float raw[4]={b.x_min,b.y_min,b.x_max,b.y_max};
      for(int j=0;j<count;++j)
        if(boxes[j].class_probabilities[0]>=strong && fall_iou(raw,boxes[j])>.15f) keep=false;
    }
    if(!keep) b.class_probabilities[0]=0;
  }
}
