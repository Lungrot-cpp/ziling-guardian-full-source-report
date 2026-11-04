#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum WorkMode {WORK_ACTIVE,WORK_WATCH,WORK_WAKING};
enum WakeReason {WAKE_BOOT,WAKE_MOTION,WAKE_PERSON,WAKE_FAULT,WAKE_MANUAL};
struct PowerPolicy {
  bool automatic,empty_started,monitor_ok,event_blocked,motion_seen;
  WorkMode mode;WakeReason reason;
  unsigned empty_count,inferences,probes,valid_probes;
  uint32_t empty_since,hold_until,last_full,last_probe,last_tick,last_valid_motion;
  uint64_t residence[3],inference_us;
  void init(uint32_t now){memset(this,0,sizeof(*this));automatic=true;last_tick=now;hold_until=now+10000;}
  void tick(uint32_t now){if((int32_t)(now-last_tick)<0)return;residence[mode]+=(uint32_t)(now-last_tick);last_tick=now;}
  void wake(WakeReason why,uint32_t now){tick(now);mode=WORK_WAKING;reason=why;hold_until=now+10000;empty_started=false;empty_count=0;}
  bool probe_due(uint32_t now)const{return mode!=WORK_WATCH || (uint32_t)(now-last_probe)>=(motion_seen && last_probe==last_valid_motion?200u:50u);}
  void motion(unsigned cells,bool valid,uint32_t now){
    probes++;last_probe=now;
    if(!valid)return;
    valid_probes++;motion_seen=true;last_valid_motion=now;
    if(cells>=18 && mode==WORK_WATCH)wake(WAKE_MOTION,now);
  }
  bool full_due(uint32_t now)const{return mode!=WORK_WATCH || (uint32_t)(now-last_full)>=2000;}
  void detected(uint32_t now,uint32_t started,unsigned candidates,bool blocked,bool healthy,unsigned busy_us){
    tick(now);inferences++;inference_us+=busy_us;last_full=started;monitor_ok=healthy;event_blocked=blocked;
    if(mode==WORK_WAKING)mode=WORK_ACTIVE;
    if(candidates || blocked || !healthy){
      empty_started=false;empty_count=0;
      if(mode==WORK_WATCH)wake(candidates?WAKE_PERSON:WAKE_FAULT,now);
      return;
    }
    if(!empty_started){empty_started=true;empty_since=now;empty_count=0;}empty_count++;
    if(automatic && mode==WORK_ACTIVE && empty_count>=10 && (uint32_t)(now-empty_since)>=30000 &&
       (int32_t)(now-hold_until)>=0)mode=WORK_WATCH;
  }
  bool configure(const char* form,uint32_t now){
    if(strcmp(form,"mode=auto") && strcmp(form,"mode=continuous"))return false;
    tick(now);automatic=!strcmp(form,"mode=auto");wake(WAKE_MANUAL,now);return true;
  }
};
extern PowerPolicy power_policy;
inline const char* work_name(WorkMode mode){return mode==WORK_WATCH?"watch":mode==WORK_WAKING?"waking":"active";}
inline const char* wake_name(WakeReason reason){const char* names[]={"boot","motion","person","fault","manual"};return names[reason];}
inline unsigned power_json(char* out,unsigned size,uint32_t now){
  auto& w=power_policy;w.tick(now);
  int n=snprintf(out,size,"{\"automatic\":%u,\"mode\":\"%s\",\"wake_reason\":\"%s\",\"sample_ms\":%u,\"inferences\":%u,\"inference_us\":%llu,\"motion_checks\":%u,\"motion_valid_checks\":%u,\"monitor_ok\":%u,\"event_blocked\":%u,\"empty_ms\":%u,\"residence_ms\":[%llu,%llu,%llu],\"camera_fps_reduced\":false,\"power_watts\":null}",
    w.automatic,work_name(w.mode),wake_name(w.reason),now,w.inferences,(unsigned long long)w.inference_us,w.probes,w.valid_probes,w.monitor_ok,w.event_blocked,
    w.empty_started?(uint32_t)(now-w.empty_since):0,(unsigned long long)w.residence[0],(unsigned long long)w.residence[1],(unsigned long long)w.residence[2]);
  if(n<0 || (unsigned)n>=size){if(size)out[0]=0;return 0;}return n;
}
