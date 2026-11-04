#pragma once
// Adapted from Erik Bochinski / TU Berlin's MIT-licensed IOU Tracker.
// https://github.com/bochinski/iou-tracker
// Copyright (c) 2017 TU Berlin, Communication Systems Group
// License and upstream source: third_party/iou_tracker/.
// Online adaptation: fixed 16 slots, one-to-one highest-IoU matching, and the
// existing board track-loss timeout. No image processing or extra detector.
#include "fall_state.h"

inline void iou_associate(const FallMachine& machine, const box* detections,
                          unsigned count, uint64_t now, uint64_t hz, int* slots) {
    bool used_detection[16]={},used_track[16]={};
    if(count>16)count=16;
    for(unsigned i=0;i<count;i++)slots[i]=-1;
    for(unsigned pass=0;pass<count;pass++) {
        float best=.10f;int di=-1,tj=-1;
        for(unsigned i=0;i<count;i++) {
            if(used_detection[i])continue;
            const box& d=detections[i];
            if(!isfinite(d.x_min)||!isfinite(d.y_min)||!isfinite(d.x_max)||!isfinite(d.y_max)||
               d.x_min>=d.x_max||d.y_min>=d.y_max)continue;
            for(unsigned j=0;j<16;j++) {
                const FallTrack& t=machine.tracks[j];
                if(used_track[j]||!t.active||now<t.last_tick||
                   now-t.last_tick>hz*FALL_TRACK_GAP_TENTHS/10)continue;
                float score=fall_iou(t.box,d);
                if(score>=best){best=score;di=i;tj=j;}
            }
        }
        if(di<0)break;
        slots[di]=tj;used_detection[di]=true;used_track[tj]=true;
    }
}
