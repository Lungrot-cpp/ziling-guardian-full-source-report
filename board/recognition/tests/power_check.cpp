#include "../software/src/power_policy.h"
PowerPolicy power_policy={};
static void check(bool v,unsigned code){if(!v){*(volatile unsigned*)0x10000000=code;asm volatile("ebreak");while(1){}}}
static void empty(PowerPolicy& p,unsigned begin,unsigned end,bool blocked=false,bool healthy=true){
  for(unsigned t=begin;t<=end;t+=500)p.detected(t,t,0,blocked,healthy,100000);
}
int main(){
  PowerPolicy p;p.init(0);empty(p,0,29500);check(p.mode==WORK_ACTIVE,1);
  p.detected(30000,30000,0,false,true,100000);check(p.mode==WORK_WATCH && p.inferences==61,2);
  check(!p.full_due(31999) && p.full_due(32000),3);
  p.motion(0,true,30000);check(!p.probe_due(30199) && p.probe_due(30200),4);
  p.motion(18,false,30200);check(p.mode==WORK_WATCH && p.probe_due(30250),5);
  p.motion(18,true,30250);check(p.mode==WORK_WAKING && p.reason==WAKE_MOTION && p.full_due(30250),6);
  p.detected(30300,30250,1,false,true,50000);check(p.mode==WORK_ACTIVE && !p.empty_started,7);
  empty(p,30500,60000);check(p.mode==WORK_ACTIVE,8);
  p.detected(60500,60500,0,false,true,100000);check(p.mode==WORK_WATCH,9);
  p.detected(62500,62500,2,false,true,100000);check(p.mode==WORK_WAKING && p.reason==WAKE_PERSON,10);
  p.init(0);empty(p,0,50000,true);check(p.mode==WORK_ACTIVE && !p.empty_started && p.event_blocked,11);
  p.init(0);empty(p,0,50000,false,false);check(p.mode==WORK_ACTIVE && !p.empty_started && !p.monitor_ok,12);
  p.init(0);empty(p,0,29500);p.detected(29999,29999,1,false,true,0);empty(p,30000,59500);check(p.mode==WORK_ACTIVE,13);
  p.detected(60000,60000,0,false,true,0);check(p.mode==WORK_WATCH,14);
  p.detected(61000,61000,0,false,false,0);check(p.mode==WORK_WAKING && p.reason==WAKE_FAULT,15);
  p.init(0);empty(p,0,29500);p.wake(WAKE_FAULT,30000);empty(p,31000,60500);check(p.mode==WORK_ACTIVE,16);
  p.detected(61000,61000,0,false,true,0);check(p.mode==WORK_WATCH,17);
  check(!p.configure("mode=sleep",62000) && p.automatic,18);
  check(p.configure("mode=continuous",62000) && !p.automatic && p.mode==WORK_WAKING,19);
  empty(p,62500,100000);check(p.mode==WORK_ACTIVE,20);
  check(p.configure("mode=auto",100000) && p.automatic,21);
  empty(p,100500,130500);check(p.mode==WORK_WATCH,22);
  p.init(0xfffff000u);p.tick(0x1000);check(p.residence[0]==8192,23);
  p.tick(0xff0);check(p.residence[0]==8192 && p.last_tick==0x1000,24);
  p.init(0);empty(p,0,30000);p.tick(32000);check(p.residence[0]==30000 && p.residence[1]==2000 && p.inference_us==6100000,25);
  power_policy=p;char json[768];check(power_json(json,sizeof(json),32000)>0 && strstr(json,"\"power_watts\":null") && strstr(json,"\"inferences\":61"),26);
  char tiny[2];check(!power_json(tiny,sizeof(tiny),32000) && !tiny[0],27);
  *(volatile unsigned*)0x10000000=0x600d;asm volatile("ebreak");return 0;
}
