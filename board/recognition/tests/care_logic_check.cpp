#include "care_logic.h"
#include "care_replay.h"
#include <string.h>
static uint32_t now_ms;
extern "C" uint32_t sys_now(){return now_ms;}
static void result(unsigned n){*(volatile unsigned*)0xf8100344=n;for(;;){}}
#define CHECK(x,n) do{if(!(x))result(n);}while(0)
int main() {
 care::Config c=care::defaults();care::State s;care::reset(s);float u,v;
 CHECK(care::valid_config(c)&&c.leds_per_room==7,1);CHECK(!care::map(c,.5,.5,u,v),2);
 CHECK(c.rotation==3&&!c.serpentine&&care::matrix_index(c,15,0)==0&&care::matrix_index(c,15,1)==1&&
       care::matrix_index(c,15,15)==15&&care::matrix_index(c,14,0)==16&&care::matrix_index(c,0,15)==255,62);
 float corners[]={.15,.2,.8,.15,.95,.9,.05,.85};
 CHECK(care::calibrate(c,corners),3);
 for(unsigned i=0;i<4;i++){CHECK(care::map(c,corners[2*i],corners[2*i+1],u,v),4);CHECK(fabsf(u-(i==1||i==2))<.003f&&fabsf(v-(i>=2))<.003f,5);}
 care::Config saved=c;float crossed[]={0,0,1,1,1,0,0,1};CHECK(!care::calibrate(c,crossed)&&!memcmp(&c,&saved,sizeof(c)),6);
 float flat[]={0,0,.3,0,.6,0,.9,0};CHECK(!care::calibrate(c,flat),7);
 float identity[]={0,0,1,0,1,1,0,1};CHECK(care::calibrate(c,identity),8);
 CHECK(care::observe(s,c,.2,.2,1,0,100,1000)&&s.room==0&&s.lit[0],9);
 care::observe(s,c,.9,.2,1,0,100,1100);CHECK(s.room==0,10);
 care::observe(s,c,.9,.2,1,0,100,1800);CHECK(s.room==1&&s.lit[1],11);
 care::expire(s,4201);CHECK(!s.valid&&!s.lit[0]&&!s.lit[1],12);
 CHECK(!care::observe(s,c,2,.5,1,0,0,4300),13);
 CHECK(!care::observe(s,c,.2,.2,1,0,2201,4400),14);
 CHECK(care::room_at(c,.5,.25)==1&&care::room_at(c,1,1)==3,15);
 CHECK(care::ROOM_COUNT==4&&care::room_at(c,.25,.25)==0&&care::room_at(c,.75,.25)==1&&
       care::room_at(c,.25,.75)==2&&care::room_at(c,.75,.75)==3&&care::room_at(c,.5,.5)==3,43);
 for(unsigned i=0;i<care::ROOM_COUNT;i++)CHECK((c.rooms[i].x1-c.rooms[i].x0)*(c.rooms[i].y1-c.rooms[i].y0)==.25f,44);
 c.leds_per_room=8;CHECK(care::valid_config(c),45);c.leds_per_room=9;CHECK(!care::valid_config(c),46);c.leds_per_room=4;
 s.valid=true;s.x=.05f;s.y=.5f;CHECK(care::pixel(s,c,268,4201)!=0&&care::pixel(s,c,272,4201)==0,47);s.valid=false;
 bool indices[256]={};for(unsigned i=0;i<256;i++){unsigned p=care::matrix_index(c,i%16,i/16);CHECK(p<256&&!indices[p],16);indices[p]=true;}
 c.rotation=1;memset(indices,0,sizeof(indices));for(unsigned i=0;i<256;i++){unsigned p=care::matrix_index(c,i%16,i/16);CHECK(p<256&&!indices[p],17);indices[p]=true;}
 c.rooms[1].x0=.4;CHECK(!care::valid_config(c),18);c=saved;CHECK(!care::observe(s,c,.2,.2,1,0,9999,5000),19);
 c=care::defaults();care::calibrate(c,identity);care::reset(s);
 CHECK(care::cell_axis(.5f)==8&&care::cell_axis(1)==15&&care::cell_axis(0)==0,28);
 CHECK(care::observe(s,c,.5f,.5f,2,2,0,100)&&s.trail[8*16+8]==1,29);
 CHECK(care::pixel(s,c,8*16+8,100)==care::wire_color(c,255,0,0,c.matrix_rgb),30);
 care::reset(s);
 care::observe(s,c,1,1,2,2,0,0xfffffff0);care::expire(s,0x20);CHECK(s.valid,20);
 CHECK(care::pixel(s,c,255,0x20)!=0,21);care::expire(s,3000);CHECK(!s.valid,22);
 c=care::defaults();care::reset(s);CHECK(c.brightness==3,31);
 for(unsigned i=0;i<256;i++)CHECK(care::pixel(s,c,i,0)==0,32);
 care::calibrate(c,identity);care::observe(s,c,.2f,.2f,1,0,100,1000);
 unsigned cell=3*16+3;s.valid=false; // Inspect the trail without the current-position overlay.
 CHECK(care::pixel(s,c,cell,3899)==care::wire_color(c,0,150,0,false),33);
 CHECK(care::pixel(s,c,cell,3900)==care::wire_color(c,0,0,150,false),34);
 CHECK(care::pixel(s,c,cell,6900)==care::wire_color(c,150,0,0,false),35);
 CHECK(care::pixel(s,c,cell,9900)==0,36);care::expire(s,9900);CHECK(!s.trail[cell],37);
 care::observe(s,c,.2f,.2f,1,0,0,11000);care::observe(s,c,.2f,.2f,1,0,0,14000);
 CHECK(s.trail_ms[cell]==14000&&care::pixel(s,c,cell,14000)==care::wire_color(c,0,255,0,false),38);
 care::reset(s);care::observe(s,c,.2f,.2f,1,0,0,0xfffffff0);s.valid=false;
 CHECK(care::pixel(s,c,cell,0x20)==care::wire_color(c,0,150,0,false),39);
 care::expire(s,0x2320);CHECK(!s.trail[cell],40);
 care::reset(s);care::observe(s,c,0,0,1,0,0,100);
 care::observe(s,c,1,1,1,0,0,500);
 // The existing position filter ends at (12,12); every diagonal step has a side neighbour.
 for(unsigned i=0;i<=12;i++)CHECK(s.trail[i*16+i]&&s.trail_ms[i*16+i]==500,48);
 for(unsigned i=0;i<12;i++)CHECK(s.trail[i*16+i+1],49);
 care::observe(s,c,0,0,1,0,0,900);
 CHECK(s.trail[6*16+6]&&s.trail_ms[6*16+6]==900,50);
 care::reset(s);care::observe(s,c,0,0,1,0,0,100);
 care::observe(s,c,1,1,2,0,0,500);CHECK(!s.trail[8*16+8],51);
 care::reset(s);care::observe(s,c,0,0,1,0,0,100);
 care::observe(s,c,1,1,1,0,0,2401);CHECK(!s.trail[8*16+8],52);
 care::reset(s);care::observe(s,c,0,.5f,1,0,0,100);care::observe(s,c,1,.5f,1,0,0,500);
 for(unsigned i=0;i<=12;i++)CHECK(s.trail[8*16+i],60);
 care::reset(s);care::observe(s,c,.5f,0,1,0,0,100);care::observe(s,c,.5f,1,1,0,0,500);
 for(unsigned i=0;i<=12;i++)CHECK(s.trail[i*16+8],61);
 care::reset(s);s.valid=true;
 const float wall_points[][2]={{.5f,.05f},{.95f,.5f},{.5f,.95f},{.05f,.5f}};
 for(unsigned wall=0;wall<4;wall++) {
  s.x=wall_points[wall][0];s.y=wall_points[wall][1];CHECK(care::nearest_wall(s)==(int)wall,53);
  for(unsigned i=256;i<288;i++)CHECK((care::pixel(s,c,i,0)!=0)==(i>=256+wall*7&&i<263+wall*7),57);
 }
 s.x=s.y=.5f;CHECK(care::nearest_wall(s)==0,58);
 s.valid=false;CHECK(care::nearest_wall(s)==-1&&care::pixel(s,c,256,0)==0,59);
 for(unsigned i=256;i<284;i++) {
  CHECK(care::pixel(s,c,i,100,true)==care::wire_color(c,255,0,0,c.strip_rgb),54);
  CHECK(care::pixel(s,c,i,500,true)==0&&care::pixel(s,c,i,1000,true)!=0,55);
 }
 for(unsigned i=284;i<288;i++)CHECK(care::pixel(s,c,i,100,true)==0,56);
 static uint8_t pixels[96*96*3];for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(i%3==0)?255:0;
 for(unsigned i=0;i<12;i++){now_ms=i*1000;replay_record(pixels,i,now_ms*1000,i==10);}
 CHECK(strstr(replay_json(),"\"recording_post\":true"),23);
 for(unsigned i=12;i<16;i++){now_ms=i*1000;replay_record(pixels,i,now_ms*1000,true);}
 CHECK(strstr(replay_json(),"\"ready\":true"),24);unsigned size=0;const uint8_t* bmp=replay_bmp(1,0,&size);
 CHECK(bmp&&size==10434&&bmp[66]==0&&bmp[67]==0xf8,25);
 CHECK(!replay_bmp(2,0,&size)&&!replay_bmp(1,64,&size),26);
 replay_reset();CHECK(!replay_bmp(1,0,&size)&&strstr(replay_json(),"\"count\":0"),27);
 memset(pixels,0,sizeof(pixels));
 for(unsigned x=0;x<96;x++){pixels[x*3]=255;pixels[(95*96+x)*3+2]=255;}
 now_ms=100000;replay_record(pixels,50,100000000,false);
 now_ms=100300;replay_record(pixels,51,100300000,true);
 now_ms=105301;replay_poll();bmp=replay_bmp(2,0,&size);
 CHECK(bmp&&bmp[66]==0&&bmp[67]==0xf8,41);
 CHECK(bmp[66+53*96*2]==0x1f&&bmp[67+53*96*2]==0,42);
 result(0x600d1234);
}
