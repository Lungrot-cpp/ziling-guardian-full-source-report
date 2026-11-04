#include "../software/src/fall_state.h"
#include "../software/src/person_fusion.h"
#include "../software/src/sensors/thermal_schedule.h"
#include "../software/src/event_ledger.h"

static void check(bool value,unsigned code) {
  if(!value){*(volatile unsigned*)0x10000000=code;asm volatile("ebreak");while(1){}}
}

int main() {
  {
    EventLedger ledger={};
    check(ledger.observe(1,2,1000,1)&&ledger.alarm(),100);
    const unsigned first=ledger.sequence;
    ledger.overflow=1;ledger.reset();
    check(!ledger.alarm()&&!ledger.count&&!ledger.overflow&&ledger.sequence==first,101);
    check(!ledger.observe(2,0,2000,2)&&!ledger.alarm(),102);
    check(ledger.observe(2,2,12000,3)&&ledger.alarm()&&ledger.sequence>first,103);
    ledger.reset();check(!ledger.alarm()&&!ledger.count,104);
  }
  check(!thermal_work_due(999999,0,0,0,0),80);
  check(thermal_work_due(1000000,0,0,0,0),81);
  check(thermal_work_due(1,0,1,0,0),82);
  check(thermal_work_due(1,0,0,1,0),83);
  check(thermal_work_due(1,0,0,0,1),84);
  check(thermal_work_due(0x100,0xfff00000u,0,0,0),85);
  FusionEvidence warm={true,1,1,0},cold={true,0,1,0},stale={false,1,1,3};
  check(!fusion_alarm(false,warm,1),1);
  check(!fusion_alarm(true,cold,1),2);
  check(!fusion_alarm(true,stale,1),3);
  check(fusion_alarm(true,warm,1),4);
  check(!fusion_alarm(true,warm,.5f),5);
  float confidence=.95f;
  {
    FallMachine fm={};int ids[1];
    box b={.4f,.1f,.6f,.7f,.95f,&confidence,0};
    fall_update(fm,&b,1,1000,1000,ids);
    b.x_min=.42f;b.x_max=.62f;
    fall_update(fm,&b,1,1500,1000,ids);
    const auto& t=fm.tracks[ids[0]];
    check(fabsf(t.display[0]-.415f)<.00001f,90);
    check(fabsf(t.box[0]-.42f)<.00001f,91);
    b.x_min=.7f;b.x_max=.9f;
    int associations[1]={ids[0]};
    fall_update(fm,&b,1,2000,1000,ids,nullptr,false,nullptr,associations);
    check(fabsf(fm.tracks[ids[0]].display[0]-.7f)<.00001f,92);
  }
  for(unsigned scenario=0;scenario<10;scenario++) {
    FallMachine fm={};int ids[1];float motion[1],recent[1];bool confirmed=false;
    unsigned first_confirm=0,expected_still_start=3000;
    unsigned dt=scenario==1?1400:scenario==7?2000:scenario==8?2800:500;
    for(unsigned now=0;now<=24000;now+=dt) {
      const bool standing=now<3000 && scenario!=3;
      box b=standing?box{.4f,.1f,.6f,.7f,.95f,&confidence,0}:box{.3f,.6f,.7f,.8f,.95f,&confidence,0};
      if(scenario==4 && !standing)b={.44f,.6f,.56f,.85f,.95f,&confidence,0};
      if(scenario==5 && !standing)b={.4f,.35f,.6f,.7f,.95f,&confidence,0};
      motion[0]=!standing && now<3000+dt?.1f:0;
      recent[0]=scenario==2 && now==8000?.1f:0;
      if(scenario==9 && now==8000)motion[0]=.1f;
      if(scenario==2)expected_still_start=8500;
      if(scenario==9)expected_still_start=8500;
      if(scenario==6 && now==8000) {
        fall_update(fm,nullptr,0,now,1000,nullptr,motion,true,recent);
        expected_still_start=8500;continue;
      }
      fall_update(fm,&b,1,now,1000,ids,motion,true,recent);
      if(ids[0]>=0 && fm.tracks[ids[0]].state==FALLEN) {
        if(!confirmed)first_confirm=now;
        confirmed=true;
        check(now>=expected_still_start+10000,10+scenario);
      }
    }
    check(confirmed==(scenario!=3 && scenario!=5 && scenario!=8),20+scenario);
    if(confirmed)check(first_confirm>=13000,30+scenario);
  }
  *(volatile unsigned*)0x10000000=0x600d;asm volatile("ebreak");return 0;
}
