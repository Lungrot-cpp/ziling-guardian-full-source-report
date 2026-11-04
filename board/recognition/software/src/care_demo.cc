#include "care_demo.h"
#include "care_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
extern "C" uint32_t sys_now(void);
namespace {
care::Config config=care::defaults();care::State position;
volatile uint32_t* const regs=(volatile uint32_t*)0xf8100500;
volatile uint32_t* const sensor_clock=(volatile uint32_t*)0xf8100308;
bool present,streaming,dirty,last_fall,alarm_active;
uint32_t last_render,last_heartbeat,last_age_tick;
int led_test_index=-1;uint32_t led_test_ms;
char json[4096];
unsigned read(unsigned off){asm volatile("fence iorw,iorw":::"memory");return regs[off/4];}
void write(unsigned off,unsigned value){regs[off/4]=value;asm volatile("fence iorw,iorw":::"memory");}
bool field(const char* form,const char* key,char* value,unsigned size) {
 unsigned n=strlen(key);
 for(const char* p=form;*p;) {
  const char* end=strchr(p,'&');if(!end)end=p+strlen(p);
  if((unsigned)(end-p)>n && !strncmp(p,key,n)&&p[n]=='=') {
   unsigned len=end-p-n-1;if(len>=size)return false;
   memcpy(value,p+n+1,len);value[len]=0;return true;
  }
  p=*end?end+1:end;
 }return false;
}
bool number(const char* form,const char* key,float& value,bool& found) {
 char text[48];found=field(form,key,text,sizeof(text));if(!found)return true;
 char* end;float v=strtof(text,&end);if(end==text||*end||!care::finite(v))return false;value=v;return true;
}
bool integer(const char* form,const char* key,unsigned& value) {
 float v=0;bool found;if(!number(form,key,v,found))return false;
 if(found){if(v<0||v>100000||floorf(v)!=v)return false;value=(unsigned)v;}return true;
}
void hardware_config() {
 if(!present)return;
 write(0x10,config.neutral_us);write(0x14,config.open_us);write(0x18,config.close_us);write(0x1c,config.run_ms);
}
uint32_t display_pixel(unsigned logical,unsigned physical,uint32_t now) {
 if(logical<256&&led_test_index>=0)return physical==(unsigned)led_test_index?0x080808:0;
 return care::pixel(position,config,logical,now,alarm_active);
}
void render(uint32_t now) {
 if(!present||!dirty||now-last_render<40||(read(4)&2))return;
 if(streaming){
  // Frame staging uses CPU DDR, not the already-full FPGA RAM.
  static uint32_t frame[care::CHAIN_LEDS];
  for(unsigned logical=0;logical<care::CHAIN_LEDS;logical++){
   unsigned physical=logical<256?care::matrix_index(config,logical%16,logical/16):logical;
   frame[physical]=display_pixel(logical,physical,now);
  }
  write(4,2);
  for(unsigned i=0;i<care::CHAIN_LEDS;i++){
   unsigned status;
   do{status=read(4);if(!(status&2))return;}while(!(status&1));
   write(12,frame[i]);
  }
  last_render=now;dirty=false;return;
 }
 for(unsigned logical=0;logical<care::CHAIN_LEDS;logical++) {
  unsigned physical=logical<256?care::matrix_index(config,logical%16,logical/16):logical;
  write(8,physical);write(12,display_pixel(logical,physical,now));
 }
 write(4,1);last_render=now;dirty=false;
}
void start_rescue() {
 alarm_active=true;dirty=true;
 if(present){write(0x28,1);write(0x20,1);write(0x20,2);}
}
struct Text {
 unsigned at;bool good;
 void add(const char* f,...) {
  if(!good)return;va_list a;va_start(a,f);int n=vsnprintf(json+at,sizeof(json)-at,f,a);va_end(a);
  if(n<0||(unsigned)n>=sizeof(json)-at){good=false;return;}at+=n;
 }
};
}
bool care_pending() {return alarm_active;}
void care_init() {
 config=care::defaults();
 care::reset(position);present=read(0)==0x43415231 && read(0x24)==care::CHAIN_LEDS;
 streaming=present&&read(0x2c)==0x53545231;
 if(present){write(0x20,0);hardware_config();dirty=true;}
}
void care_update(const TelemetryTrack* tracks,unsigned count,uint32_t capture_us) {
 const uint32_t now=sys_now(),age=(*sensor_clock-capture_us)/1000;int chosen=-1;bool fallen=false;
 for(unsigned i=0;i<count;i++) {
  const auto& t=tracks[i];if(t.x0>=t.x1||t.y0>=t.y1||t.x1>=1920||t.y1>=1080)continue;
  if(t.state==2)fallen=true;
  if(chosen<0 || t.id==position.track || (tracks[chosen].id!=position.track&&t.detector>tracks[chosen].detector))chosen=i;
 }
 if(chosen>=0) {
  const auto& t=tracks[chosen];
  care::observe(position,config,(t.x0+t.x1)/3840.f,t.y1/1080.f,t.id,t.state,age,now);
  if(age<=care::STALE_MS) {
   if(fallen&&!last_fall&&!alarm_active)start_rescue();
   last_fall=fallen;
  }
 }
 dirty=true;care_poll();
}
extern "C" void care_poll() {
 uint32_t now=sys_now();
 if(led_test_index>=0&&(uint32_t)(now-led_test_ms)>5000){led_test_index=-1;dirty=true;}
 // Keep aging independent of detections, without scanning 256 cells on every
 // network poll inside accelerator waits.
 if((uint32_t)(now-last_age_tick)>=100){
  if(care::expire(position,now)||care::has_trail(position)||alarm_active)dirty=true;
  last_age_tick=now;
 }
 if(present&&now-last_heartbeat>=250){write(0x28,1);last_heartbeat=now;}
 render(now);
}
const char* care_json() {
 care_poll();uint32_t now=sys_now();Text t={0,true};unsigned door=present?read(0x20):0;
 t.add("{\"hardware\":%s,\"calibrated\":%s,\"valid\":%s,\"track\":%u,\"x\":%.5f,\"y\":%.5f,\"age_ms\":%u,\"room\":%d,\"wall\":%d,\"risk\":%u,\"countdown_ms\":%u,\"alarm\":%s,\"armed\":%s,\"door_state\":%u,\"remaining_ms\":%u,\"led_test_index\":%d,\"ram_only\":true,\"rooms\":[",
  present?"true":"false",config.calibrated?"true":"false",position.valid?"true":"false",position.track,
  position.x,position.y,position.have_observation?(uint32_t)(now-position.observed_ms):0,position.room,care::nearest_wall(position),position.risk,
  0,alarm_active?"true":"false",door&1?"true":"false",(door>>1)&7,door>>4,led_test_index);
 for(unsigned i=0;i<care::ROOM_COUNT;i++){const auto&r=config.rooms[i];t.add("%s{\"rect\":[%.5f,%.5f,%.5f,%.5f],\"lit\":%s}",i?",":"",r.x0,r.y0,r.x1,r.y1,(alarm_active?now%1000<500:position.lit[i])?"true":"false");}
 t.add("],\"config\":{\"rotation\":%u,\"serpentine\":%u,\"brightness\":%u,\"leds_per_room\":%u,\"matrix_rgb\":%u,\"strip_rgb\":%u,\"neutral_us\":%u,\"open_us\":%u,\"close_us\":%u,\"run_ms\":%u,\"corners\":[",
  config.rotation,config.serpentine,config.brightness,config.leds_per_room,config.matrix_rgb,config.strip_rgb,
  config.neutral_us,config.open_us,config.close_us,config.run_ms);
 for(unsigned i=0;i<8;i++)t.add("%s%.5f",i?",":"",config.corners[i]);t.add("]},\"trail_step_ms\":%u,\"trail_ttl_ms\":%u,\"trail_ages\":[",care::TRAIL_STEP_MS,care::TRAIL_TTL_MS);
 for(unsigned i=0;i<256;i++)t.add("%s%d",i?",":"",position.trail[i]?(int)(uint32_t)(now-position.trail_ms[i]):-1);
 t.add("]}");
 if(!t.good)strcpy(json,"{\"error\":\"care status too large\"}");return json;
}
bool care_configure(const char* form) {
 care::Config next=config;
 if(!integer(form,"rotation",next.rotation)||!integer(form,"brightness",next.brightness)||
 !integer(form,"leds_per_room",next.leds_per_room)||!integer(form,"neutral_us",next.neutral_us)||
 !integer(form,"open_us",next.open_us)||!integer(form,"close_us",next.close_us)||!integer(form,"run_ms",next.run_ms))return false;
 const char* names[]={"serpentine","matrix_rgb","strip_rgb"};bool* flags[]={&next.serpentine,&next.matrix_rgb,&next.strip_rgb};
 for(unsigned i=0;i<3;i++){unsigned n=*flags[i];if(!integer(form,names[i],n)||n>1)return false;*flags[i]=n;}
 for(unsigned i=0;i<care::ROOM_COUNT;i++)for(unsigned j=0;j<4;j++) {
  char key[8];snprintf(key,sizeof(key),"r%u%u",i,j);bool found;
  float* values[]={&next.rooms[i].x0,&next.rooms[i].y0,&next.rooms[i].x1,&next.rooms[i].y1};
  if(!number(form,key,*values[j],found))return false;
 }
 float corners[8];bool any=false,all=true;
 for(unsigned i=0;i<8;i++){char key[8];snprintf(key,sizeof(key),"p%u",i);bool found;
  if(!number(form,key,corners[i],found))return false;any|=found;all&=found;}
 if(any&&(!all||!care::calibrate(next,corners)))return false;
 if(!care::valid_config(next))return false;
 bool servo_changed=next.neutral_us!=config.neutral_us||next.open_us!=config.open_us||next.close_us!=config.close_us||next.run_ms!=config.run_ms;
 if(servo_changed&&present&&(read(0x20)&1))return false;
 config=next;care::reset(position);if(servo_changed)hardware_config();dirty=true;return true;
}
bool care_action(const char* form) {
 char cmd[24];if(!field(form,"cmd",cmd,sizeof(cmd)))return false;
 if(!strcmp(cmd,"led_test")) {
  unsigned index=256;if(!present||!integer(form,"index",index)||index>=256)return false;
  led_test_index=index;led_test_ms=sys_now();dirty=true;return true;
 }
 if(!strcmp(cmd,"led_test_stop")){led_test_index=-1;dirty=true;return true;}
 if(!strcmp(cmd,"clear_trail")){memset(position.trail,0,sizeof(position.trail));position.prev_cell=256;dirty=true;return true;}
 if(!strcmp(cmd,"reset")){last_fall=false;position.risk=0;dirty=true;}
 if(!strcmp(cmd,"cancel")||!strcmp(cmd,"reset")){alarm_active=false;dirty=true;if(present)write(0x20,4);return true;}
 if(!strcmp(cmd,"sos")){start_rescue();return true;}
 if(!present)return false;
 unsigned command;
 if(!strcmp(cmd,"arm"))command=1;
 else if(!strcmp(cmd,"disarm"))command=0;
 else if(!strcmp(cmd,"open"))command=2;
 else if(!strcmp(cmd,"close"))command=3;
 else if(!strcmp(cmd,"stop"))command=4;
 else return false;
 if((command==2||command==3)&&!(read(0x20)&1))return false;
 write(0x28,1);write(0x20,command);return true;
}
