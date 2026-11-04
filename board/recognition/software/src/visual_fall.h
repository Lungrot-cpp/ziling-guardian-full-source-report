#pragma once
// Shared observation clock; detector box jitter must not reset image stillness.
#include "fall_state.h"
struct VisualFallState {
    unsigned id,count;
    FallState state;
    // still_ms stores ticks; milliseconds only when hz=1000.
    uint64_t last_tick,falling_tick,still_ms,recovery_tick;
    float reference_height,reference_center_y,still_x,still_y;
    bool missing;
};
inline void visual_fall_observe(FallTrack& track,VisualFallState& p,uint64_t now,float motion,uint64_t hz=1000) {
    if(p.id!=track.id){p=VisualFallState();p.id=track.id;}
    const float height=track.box[3]-track.box[1];
    const float center=(track.box[1]+track.box[3])*.5f;
    const float x=(track.box[0]+track.box[2])*.5f;
    if(p.state==NORMAL) {
        if(track.state==FALLING) {
            p.state=FALLING;p.reference_height=track.reference_height;
            p.reference_center_y=track.reference_center_y;
        } else if(motion>=.03f) {
            for(int i=0;i<track.sample_count-1;i++) {
                const FallSample& s=track.samples[(track.first_sample+i)%32];
                if(now-s.tick>hz*26/10 || s.height<.12f || s.ratio>=.85f)continue;
                const float top=s.center_y-s.height*.5f;
                if(height<=s.height*.80f && center-s.center_y>=s.height*.22f &&
                   track.box[1]-top>=s.height*.30f) {
                    p.state=FALLING;p.reference_height=s.height;p.reference_center_y=s.center_y;break;
                }
            }
        }
        p.falling_tick=now;p.count=0;p.still_ms=0;p.recovery_tick=0;
    } else {
        const uint64_t gap=now-p.last_tick;
        if(gap>hz*26/10){p.count=0;p.still_ms=0;p.recovery_tick=0;}
        const bool upright=height>=p.reference_height*.85f && center<=p.reference_center_y+.05f;
        if(upright) {
            if(!p.recovery_tick)p.recovery_tick=now;
            if(now-p.recovery_tick>=hz){p.state=NORMAL;p.count=0;p.still_ms=0;}
        } else p.recovery_tick=0;
        // Small head/hand changes are allowed; gross image motion or displacement resets the clock.
        const bool still=isfinite(motion) && motion>=0 && motion<=.08f && (!p.count ||
            (fabsf(x-p.still_x)<=p.reference_height*.12f && fabsf(center-p.still_y)<=p.reference_height*.12f));
        if(p.state!=NORMAL && !upright && still) {
            if(!p.count){p.still_x=x;p.still_y=center;p.still_ms=0;}
            else if(!p.missing && gap<=hz*26/10)p.still_ms+=gap;
            ++p.count;
            if(p.state==FALLING && p.count>=3 && p.still_ms>=hz*10)p.state=FALLEN;
        } else {p.count=0;p.still_ms=0;}
    }
    p.last_tick=now;p.missing=false;
    track.state=p.state;track.falling_tick=p.falling_tick;
    track.reference_height=p.reference_height;track.reference_center_y=p.reference_center_y;
    track.still_count=p.count;track.still_tick=now-p.still_ms;track.still_last=now;
    track.visual_score=fall_min(1.0f,track.action_score+(p.state!=NORMAL ? .25f*fall_min(1.0f,(float)p.still_ms/(hz*10)) : 0));
    if(p.state==FALLEN){track.fallen_center_y=center;track.fallen_height=height;}
}
