#pragma once
#include "fall_state.h"

// Single-person trial: keep the current identity without enlarging or inventing boxes.
inline int single_person_candidate(const box* detections,unsigned count,const FallMachine& machine,
                                   uint64_t now,uint64_t hz) {
    if(count>16)count=16;
    const FallTrack* target=nullptr;
    for(const auto& t:machine.tracks)
        if(t.active && now>=t.last_tick && now-t.last_tick<=hz*FALL_TRACK_GAP_TENTHS/10 &&
           (!target || t.last_tick>target->last_tick))target=&t;
    int best=-1;float best_score=-1e9f;
    for(unsigned i=0;i<count;i++) {
        const box& d=detections[i];
        if(!d.class_probabilities || !isfinite(d.class_probabilities[0]) || d.class_probabilities[0]<.40f ||
           !isfinite(d.objectness) || d.objectness<.40f || !isfinite(d.x_min) || !isfinite(d.y_min) ||
           !isfinite(d.x_max) || !isfinite(d.y_max) || d.x_min<0 || d.y_min<0 ||
           d.x_max>1 || d.y_max>1 || d.x_min>=d.x_max || d.y_min>=d.y_max)continue;
        float score=d.class_probabilities[0];
        if(target) {
            const float dt=fall_min(.5f,(float)(now-target->last_tick)/hz);
            const float predicted[4]={target->box[0]+target->velocity_x*dt,target->box[1]+target->velocity_y*dt,
                                      target->box[2]+target->velocity_x*dt,target->box[3]+target->velocity_y*dt};
            const float dx=(predicted[0]+predicted[2]-d.x_min-d.x_max)*.5f;
            const float dy=(predicted[1]+predicted[3]-d.y_min-d.y_max)*.5f;
            const float raw_dx=(target->box[0]+target->box[2]-d.x_min-d.x_max)*.5f;
            const float raw_dy=(target->box[1]+target->box[3]-d.y_min-d.y_max)*.5f;
            const float distance=fall_min(sqrtf(dx*dx+dy*dy),sqrtf(raw_dx*raw_dx+raw_dy*raw_dy));
            const float iou=fall_max(fall_iou(predicted,d),fall_iou(target->box,d));
            const bool rising=target->state==FALLEN && fabsf(dx)<.15f && dy>0 && dy<.45f &&
                              d.y_max-d.y_min>=(target->box[3]-target->box[1])*1.2f;
            if(iou<.05f && distance>.25f && !rising)continue;
            score=iou-distance*.2f+.05f*score;
        }
        if(score>best_score){best_score=score;best=(int)i;}
    }
    return best;
}
