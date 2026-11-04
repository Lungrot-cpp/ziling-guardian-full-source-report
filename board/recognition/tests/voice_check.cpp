#include "../software/src/voice.cc"
#include "../software/src/yellow_timeout.h"
static volatile unsigned* const test=reinterpret_cast<volatile unsigned*>(0x10000000u);
static void check_yellow_timeout() {
  YellowTimeoutState s{};
  bool ok=!yellow_timeout_alarm(s,1,true,0,1000);
  for(unsigned t=2000;t<=8000;t+=2000)ok&=!yellow_timeout_alarm(s,1,true,t,1000);
  ok&=!yellow_timeout_alarm(s,1,true,9999,1000);
  ok&=yellow_timeout_alarm(s,1,true,10000,1000);
  ok&=!yellow_timeout_alarm(s,2,true,10001,1000);
  ok&=s.since==10001;
  ok&=!yellow_timeout_alarm(s,2,true,13000,1000)&&s.since==13000;
  ok&=!yellow_timeout_alarm(s,2,true,12000,1000)&&s.since==12000;
  ok&=!yellow_timeout_alarm(s,2,false,12001,1000)&&!s.active;
  ok&=!yellow_timeout_alarm(s,0,true,12002,1000)&&!s.active;
  if(!ok){test[0]=0xbad5;asm volatile("ebreak");}
}
static void update(unsigned state,uint32_t ms,unsigned marker) {
  uint32_t before,after;
  asm volatile("csrr %0,mstatus" : "=r"(before));
  test[1]=marker;voice_update(state,ms);
  asm volatile("csrr %0,mstatus" : "=r"(after));
  if((before^after)&8u){test[0]=0xbad3;asm volatile("ebreak");}
}
extern "C" int main() {
  check_yellow_timeout();
  const unsigned scenario=test[0];
  if(scenario!=5)asm volatile("csrsi mstatus,8");
  shared_uart[0]=0x4c;
  if(scenario==7)for(unsigned i=0;i<128;i++)shared_uart[0]=i;
  bool ready=voice_init(scenario==5?0xffffff00u:0);
  if(ready!=(scenario!=0)){test[0]=0xbad1;asm volatile("ebreak");}
  switch(scenario) {
    case 0:update(2,1000,1);update(2,90000,2);break;
    case 1:
      update(0,999,1);update(0,1000,2);update(0,1001,3);
      update(1,2000,4);update(1,3000,5);
      test[2]=1;update(2,3500,6);test[2]=0;update(0,4000,7);break;
    case 2:
      update(2,1000,1);test[2]=1;update(2,2000,2);
      test[2]=0;update(2,5000,3);update(2,9999,4);update(2,10000,5);
      update(2,10001,6);update(2,24999,7);update(2,25000,8);
      update(0,26000,9);break;
    case 3:
      test[2]=1;update(1,1000,1);update(0,2000,2);
      test[2]=0;update(0,2500,3);update(1,3000,4);update(3,4000,5);break;
    case 4:
      update(2,1000,1);update(2,20000,2);
      if(voice_enabled){test[0]=0xbad2;asm volatile("ebreak");}break;
    case 5:update(2,743,1);update(2,744,2);break;
    case 6:
      update(2,1000,1);update(2,20000,2);
      if(voice_enabled){test[0]=0xbad4;asm volatile("ebreak");}break;
    case 7:update(2,1000,1);break;
  }
  shared_uart[0]=0x52;
  test[0]=0x600d;asm volatile("ebreak");return 0;
}
