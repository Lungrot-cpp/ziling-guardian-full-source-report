// Synthetic spatial fixtures exercise the production rule, never demo data.
#include "../software/src/person_fusion.h"
#include "../software/src/yellow_timeout.h"
static void check(bool value,unsigned code) {
  if(!value){*(volatile unsigned*)0x10000000=code;asm volatile("ebreak");while(1){}}
}
int main() {
  SensorSnapshot s={};s.valid=1;s.ambient_centi=2400;s.humidity_centi=5000;
  const float affine[]={1,0,0,0,1,0},region[]={0,0,1,1};
  box left={},right={};left.x_min=.1f;left.x_max=.4f;left.y_min=.1f;left.y_max=.9f;
  right=left;right.x_min=.6f;right.x_max=.9f;
  for(unsigned row=0;row<24;row++)for(unsigned col=0;col<32;col++)s.heat[row*32+col]=col<16?3200:2400;
  FusionEvidence warm=fusion_evidence(left,s,true,affine,region);
  FusionEvidence cold=fusion_evidence(right,s,true,affine,region);
  check(fusion_person_confirmed(true,warm),1);
  check(cold.valid && cold.reason==10 && !fusion_person_confirmed(true,cold),2);
  check(fusion_person_confirmed(false,cold),3);
  check(!FUSION_CALIBRATED && fusion_person_confirmed(true,fusion_evidence(left,s)),4);
  check(fusion_person_confirmed(false,fusion_evidence(left,s)),5);
  s.valid=0;check(fusion_person_confirmed(true,fusion_evidence(left,s,true,affine,region)),6);s.valid=1;
  s.thermal_age_us=V6_THERMAL_AGE_US+1;
  check(fusion_person_confirmed(true,fusion_evidence(left,s,true,affine,region)),7);s.thermal_age_us=0;
  s.environment_age_us=2000001;
  check(fusion_person_confirmed(true,fusion_evidence(left,s,true,affine,region)),8);s.environment_age_us=0;
  const float narrow[]={.2f,0,1,1},singular[]={0,0,0,0,0,0};
  check(fusion_person_confirmed(true,fusion_evidence(left,s,true,affine,narrow)),9);
  check(fusion_person_confirmed(true,fusion_evidence(left,s,true,singular,region)),10);
  box distant=left;distant.x_max=.12f;distant.y_max=.12f;
  const FusionEvidence small=fusion_evidence(distant,s,true,affine,region);
  check(!small.valid && small.reason==9 && fusion_person_confirmed(true,small),18);
  for(auto& t:s.heat)t=-32768;
  check(fusion_person_confirmed(true,fusion_evidence(left,s,true,affine,region)),11);
  warm.thermal=.499f;check(!fusion_person_confirmed(true,warm),12);
  warm.thermal=.5f;check(fusion_person_confirmed(true,warm),13);
  warm.thermal=NAN;check(fusion_person_confirmed(true,warm),14);
  YellowTimeoutState timer={};
  check(!yellow_timeout_alarm(timer,7,true,100,1000),15);
  check(yellow_timeout_alarm(timer,7,true,10100,1000)==false,16); // Observation gap still resets.
  for(unsigned now=10600;now<=20100;now+=500)yellow_timeout_alarm(timer,7,true,now,1000);
  check(yellow_timeout_alarm(timer,7,true,20100,1000),17);
  *(volatile unsigned*)0x10000000=0x600d;asm volatile("ebreak");return 0;
}
