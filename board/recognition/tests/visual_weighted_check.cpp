#include "../software/src/visual_fall.h"
#include "../software/src/person_fusion.h"
#include "../software/src/camera_input.h"
static void check(bool value,unsigned code) {
  if(!value){*(volatile unsigned*)0x10000000=code;asm volatile("ebreak");while(1){}}
}
static unsigned char image[96*96*3];
static int8_t input[96*96*3],lookup[256];
int main() {
  for(unsigned i=0;i<256;i++)lookup[i]=(int)i-128;
  for(unsigned i=0;i<sizeof(image);i++)image[i]=i%256;
  prepare_camera_input(image,input,lookup);
  for(unsigned i=0;i<sizeof(image);i++)check(input[i]==lookup[image[i]],1);
  check(camera_y(0)==0 && camera_y(.5f)==.5f && camera_y(1)==1,2);
  FusionEvidence warm={true,1,1,0},cold={true,0,1,10},invalid={false,1,1,1};
  for(unsigned scenario=0;scenario<3;scenario++) {
    VisualFallState p={};FallTrack t={};t.id=1;t.state=FALLING;
    t.reference_height=.6f;t.reference_center_y=.4f;t.action_score=.8f;
    t.box[0]=.3f;t.box[1]=.6f;t.box[2]=.7f;t.box[3]=.8f;
    visual_fall_observe(t,p,0,.1f);
    check(t.state==FALLING,3);
    unsigned first=0;
    for(unsigned now=500;now<=23000;now+=500) {
      if(scenario==1 && now==5500){p.missing=true;continue;}
      float motion=scenario==2 && now==5500?.2f:0;
      visual_fall_observe(t,p,now,motion);
      if(t.state==FALLEN && !first)first=now;
      const bool eligible=t.state==FALLEN && fall_still(t,now,1000);
      check(!fusion_alarm(eligible,cold,t.visual_score),4);
      check(!fusion_alarm(eligible,invalid,t.visual_score),5);
      if(now<10500)check(!fusion_alarm(eligible,warm,t.visual_score),6);
    }
    check(first== (scenario==1?11500:scenario==2?16000:10500),7+scenario);
    check(fusion_alarm(t.state==FALLEN && fall_still(t,23000,1000),warm,t.visual_score),12);
  }
  VisualFallState p={};FallTrack t={};t.id=2;
  t.box[0]=.3f;t.box[1]=.6f;t.box[2]=.7f;t.box[3]=.8f;
  for(unsigned now=0;now<=15000;now+=500)visual_fall_observe(t,p,now,0);
  check(t.state==NORMAL,13);
  check(!fusion_alarm(true,warm,.63f) && fusion_alarm(true,warm,.64f),14);
  *(volatile unsigned*)0x10000000=0x600d;asm volatile("ebreak");return 0;
}
