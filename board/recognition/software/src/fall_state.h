// V6 box-based fall trial; adapted locally from tiny5-dsp-live-test/model/fall_state.h.
#ifndef FALL_STATE_H
#define FALL_STATE_H

#include <math.h>
#include <stdint.h>
#include "model/yolo.h"

enum FallState { NORMAL = 0, FALLING = 1, FALLEN = 2 };
const unsigned FALL_TRACK_GAP_TENTHS = 28; // Two missed frames at the current ~0.86 s cadence.

struct FallSample {
   uint64_t tick;
   float center_x;
   float center_y;
   float ratio;
   float height;
};

struct FallTrack {
   bool active;
   unsigned id;
   FallState state;
   float box[4];
   float display[4];
   int missed;
   uint64_t last_tick;
   uint64_t falling_tick;
   uint64_t upright_tick;
   bool upright_pending;
   bool motion_seen;
   uint64_t motion_tick;
   float fallen_center_y;
   float fallen_height;
   float reference_height, reference_center_y;
   uint64_t still_tick,still_last;
   unsigned still_count;
   float still_x,still_y,still_ratio;
   float visual_score,action_score;
   float velocity_x,velocity_y;
   FallSample samples[32];
   int first_sample;
   int sample_count;
};

struct FallMachine {
   FallTrack tracks[16];
   unsigned next_id;
};

inline float fall_max(float a, float b) { return a > b ? a : b; }
inline float fall_min(float a, float b) { return a < b ? a : b; }

inline float fall_iou(const float a[4], const box& b) {
   float x = fall_max(0.0f, fall_min(a[2], b.x_max) - fall_max(a[0], b.x_min));
   float y = fall_max(0.0f, fall_min(a[3], b.y_max) - fall_max(a[1], b.y_min));
   float intersection = x * y;
   float area_a = (a[2] - a[0]) * (a[3] - a[1]);
   float area_b = (b.x_max - b.x_min) * (b.y_max - b.y_min);
   return intersection / fall_max(area_a + area_b - intersection, 1e-9f);
}

inline bool fall_still(const FallTrack& track, uint64_t now, uint64_t hz) {
   return track.still_count>=3 && now==track.still_last && now-track.still_tick>=hz*10;
}

inline void fall_measure(FallTrack& track, uint64_t now, uint64_t hz,
                         bool require_motion=false,bool image_still=true) {
   const float width = fall_max(track.box[2] - track.box[0], 1e-6f);
   const float height = fall_max(track.box[3] - track.box[1], 1e-6f);
   const float center_x = (track.box[0] + track.box[2]) * 0.5f;
   const float center_y = (track.box[1] + track.box[3]) * 0.5f;
   const float ratio = width * (1920.0f / 1080.0f) / height;
   const uint64_t window = hz * 6;
   const bool has_previous = track.sample_count > 0;
   FallSample baseline = {};
   if (has_previous) baseline = track.samples[(track.first_sample + track.sample_count - 1) % 32];
   if (track.sample_count == 32) {
      track.first_sample = (track.first_sample + 1) % 32;
      --track.sample_count;
   }
   track.samples[(track.first_sample + track.sample_count) % 32] = {now, center_x, center_y, ratio, height};
   ++track.sample_count;
   // shortcut: box motion sampled at ~0.86 s; fast or intentional falls remain ambiguous without more input.
   while (track.sample_count > 1 && now - track.samples[track.first_sample].tick > window) {
      track.first_sample = (track.first_sample + 1) % 32;
      --track.sample_count;
   }
   // The transition can span several frames; compare against a recent upright sample.
   for (int i = 0; i < track.sample_count - 1; ++i) {
      const FallSample& sample = track.samples[(track.first_sample + i) % 32];
      if (now-sample.tick<=hz*26/10 && sample.ratio < 0.85f && (baseline.ratio >= 0.85f || sample.center_y < baseline.center_y))
         baseline = sample;
   }
   const float drop = center_y - baseline.center_y;
   const float ratio_gain = ratio - baseline.ratio;
   const float relative_drop=drop/fall_max(baseline.height,1e-6f);
   const float collapse=1-height/fall_max(baseline.height,1e-6f);
   const bool moving=!require_motion || (track.motion_seen && now-track.motion_tick<=hz*26/10);
   if (!image_still || (track.still_count && (now-track.still_last>hz*26/10 ||
       fabsf(center_x-track.still_x)>.03f || fabsf(center_y-track.still_y)>.03f ||
       fabsf(ratio-track.still_ratio)>.15f))) track.still_count=0;
   if(image_still) {
      if(!track.still_count){track.still_tick=now;track.still_x=center_x;track.still_y=center_y;track.still_ratio=ratio;}
      ++track.still_count;track.still_last=now;
   }
   const float posture=fall_min(1.0f,fall_max(ratio/1.2f,collapse/.45f));
   const float descent=fall_min(1.0f,fall_max(0.0f,relative_drop/.4f));
   const float still=track.still_count ? fall_min(1.0f,(float)(now-track.still_tick)/(hz*10)) : 0;
   const float action=.4f*posture+.35f*descent;
   track.action_score=track.state==NORMAL?action:fall_max(track.action_score,action);
   track.visual_score=fall_min(1.0f,track.action_score+(track.state!=NORMAL?.25f*still:0));

   if (track.state == NORMAL && has_previous && now-baseline.tick<=hz*26/10 && baseline.height >= .12f &&
       ((ratio >= 1.2f && relative_drop >= .2f && ratio_gain >= .4f) ||
        (collapse>=.45f && relative_drop>=.25f)) && moving) {
      track.state = FALLING;
      track.falling_tick = now;
      track.reference_height = baseline.height;
      track.reference_center_y = baseline.center_y;
   } else if (track.state == FALLING) {
      if ((ratio >= 1.2f || height<=track.reference_height*.55f) && now - track.falling_tick >= hz * 10 && fall_still(track, now, hz) && image_still) {
         track.state = FALLEN;
         track.fallen_center_y = center_y;
         track.fallen_height = height;
      } else if ((height>=track.reference_height*.85f && center_y<=track.reference_center_y+.05f) || now-track.falling_tick>hz*30) {
         track.state = NORMAL;
      }
   } else if (track.state == FALLEN) {
      if (height>=track.reference_height*.85f && center_y<=track.reference_center_y+.05f) {
         if (!track.upright_pending) {
            track.upright_tick = now;
            track.upright_pending = true;
         } else if (now - track.upright_tick >= hz) {
            track.state = NORMAL;
            track.upright_pending = false;
         }
      } else {
         track.upright_pending = false;
      }
   }
}

inline void fall_update(FallMachine& machine, const box* detections, int count, uint64_t now, uint64_t hz,
                        int* detection_tracks = nullptr,const float* motion=nullptr,bool motion_valid=false,const float* recent=nullptr,
                        const int* association_override=nullptr) {
   bool matched[16] = {};
   if (detection_tracks) for (int i = 0; i < count; ++i) detection_tracks[i] = -1;
   for (int j = 0; j < 16; ++j)
      if (machine.tracks[j].active && now - machine.tracks[j].last_tick > hz * FALL_TRACK_GAP_TENTHS / 10)
         machine.tracks[j].active = false;
   int associations[16];bool assigned[16]={};
   for(unsigned i=0;i<16;i++)associations[i]=-1;
   // Match the strongest pair first; a missed target can move for at most 0.5 s.
   if(association_override) {
      for(int i=0;i<count&&i<16;i++) {
         int j=association_override[i];
         if(j>=0&&j<16&&machine.tracks[j].active&&!matched[j]) {
            associations[i]=j;assigned[i]=true;matched[j]=true;
         }
      }
   }
   for(unsigned pass=0;!association_override&&pass<16;pass++) {
      float best_score=-1e9f;int best_i=-1,best_j=-1;
      for(int i=0;i<count && i<16;i++) {
         const box& d=detections[i];
         if(assigned[i] || !isfinite(d.objectness) || d.objectness<.40f ||
            !isfinite(d.x_min) || !isfinite(d.x_max) || !isfinite(d.y_min) || !isfinite(d.y_max) ||
            d.x_min<0 || d.y_min<0 || d.x_max>1 || d.y_max>1 || d.x_min>=d.x_max || d.y_min>=d.y_max)continue;
         for(int j=0;j<16;j++) {
            const FallTrack& t=machine.tracks[j];
            if(!t.active || matched[j])continue;
            float dt=fall_min(.5f,(float)(now-t.last_tick)/hz);
            float predicted[4]={t.box[0]+t.velocity_x*dt,t.box[1]+t.velocity_y*dt,
                                t.box[2]+t.velocity_x*dt,t.box[3]+t.velocity_y*dt};
            float dx=(predicted[0]+predicted[2]-d.x_min-d.x_max)*.5f;
            float dy=(predicted[1]+predicted[3]-d.y_min-d.y_max)*.5f;
            float raw_dx=(t.box[0]+t.box[2]-d.x_min-d.x_max)*.5f;
            float raw_dy=(t.box[1]+t.box[3]-d.y_min-d.y_max)*.5f;
            float distance=fall_min(sqrtf(dx*dx+dy*dy),sqrtf(raw_dx*raw_dx+raw_dy*raw_dy));
            float iou=fall_max(fall_iou(predicted,d),fall_iou(t.box,d));
            bool rising=t.state==FALLEN && fabsf(dx)<.15f && dy>0 && dy<.45f &&
                        d.y_max-d.y_min>=(t.box[3]-t.box[1])*1.2f;
            if(iou<.05f && distance>.25f && !rising)continue;
            float score=iou-distance*.2f;
            if(score>best_score){best_score=score;best_i=i;best_j=j;}
         }
      }
      if(best_i<0)break;
      associations[best_i]=best_j;assigned[best_i]=true;matched[best_j]=true;
   }
   for (int i = 0; i < count; ++i) {
      const box& detection = detections[i];
      if (!isfinite(detection.x_min) || !isfinite(detection.y_min) ||
          !isfinite(detection.x_max) || !isfinite(detection.y_max) || !isfinite(detection.objectness) ||
          detection.objectness < 0.40f || detection.x_min < 0.0f || detection.y_min < 0.0f ||
          detection.x_max > 1.0f || detection.y_max > 1.0f ||
          detection.x_min >= detection.x_max || detection.y_min >= detection.y_max) continue;

      int best=i<16?associations[i]:-1;
      if (best < 0) {
         for (int j = 0; j < 16; ++j) {
            if (!machine.tracks[j].active) { best = j; break; }
         }
      }
      if (best < 0) break;

      FallTrack& track = machine.tracks[best];
      if (!track.active) {
         track = FallTrack();
         track.active = true;
         track.id = ++machine.next_id;
         track.display[0] = detection.x_min;
         track.display[1] = detection.y_min;
         track.display[2] = detection.x_max;
         track.display[3] = detection.y_max;
      } else {
         const float alpha = fall_iou(track.box, detection) >= 0.35f ? 0.75f : 1.0f;
         const float raw[4] = {detection.x_min, detection.y_min, detection.x_max, detection.y_max};
         for (int k = 0; k < 4; ++k) track.display[k] += alpha * (raw[k] - track.display[k]);
      }
      if(track.sample_count && now>track.last_tick) {
         float dt=(float)(now-track.last_tick)/hz;
         track.velocity_x=fall_max(-1.0f,fall_min(1.0f,((detection.x_min+detection.x_max-track.box[0]-track.box[2])*.5f)/dt));
         track.velocity_y=fall_max(-1.0f,fall_min(1.0f,((detection.y_min+detection.y_max-track.box[1]-track.box[3])*.5f)/dt));
      }
      track.box[0] = detection.x_min;
      track.box[1] = detection.y_min;
      track.box[2] = detection.x_max;
      track.box[3] = detection.y_max;
      track.missed = 0;
      track.last_tick = now;
      matched[best] = true;
      if (detection_tracks) detection_tracks[i] = best;
      if(motion && motion_valid && motion[i]>=0.03f) {
         track.motion_seen=true;track.motion_tick=now;
      }
      const float current=recent?recent[i]:motion?motion[i]:0;
      fall_measure(track, now, hz,motion!=nullptr,!motion || (motion_valid && current>=0 && current<=0.02f && motion[i]>=0 && motion[i]<=0.02f));
   }
   for (int j = 0; j < 16; ++j) {
      FallTrack& track = machine.tracks[j];
      if (track.active && !matched[j]) {
         ++track.missed;
         // Missing observations never count as stillness or recovery.
         track.still_count=0;track.upright_pending=false;
      }
   }
}

#endif
