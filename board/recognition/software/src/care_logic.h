#pragma once
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

namespace care {
static const unsigned ROOM_COUNT=4,WALL_COUNT=4,CHAIN_LEDS=288;
static const uint32_t STALE_MS=2200,ROOM_HOLD_MS=600,LIGHT_HOLD_MS=1500;
static const uint32_t TRAIL_STEP_MS=3000,TRAIL_TTL_MS=3*TRAIL_STEP_MS;
struct Room {float x0,y0,x1,y1;};
struct Config {
 bool calibrated,serpentine;unsigned rotation,brightness,leds_per_room; // Saved/API key retained; LEDs per wall.
 bool matrix_rgb,strip_rgb;
 float corners[8],h[9];Room rooms[ROOM_COUNT];
 unsigned neutral_us,open_us,close_us,run_ms;
};
inline Config defaults() {
 Config c={};c.rotation=3;c.serpentine=false;c.brightness=3;c.leds_per_room=7;
 c.rooms[0]={0,0,.5f,.5f};c.rooms[1]={.5f,0,1,.5f};
 c.rooms[2]={0,.5f,.5f,1};c.rooms[3]={.5f,.5f,1,1};
 c.neutral_us=1500;c.open_us=1650;c.close_us=1350;c.run_ms=400;return c;
}
inline bool finite(float x){return isfinite(x);}
inline bool map(const Config& c,float x,float y,float& u,float& v) {
 const float d=c.h[6]*x+c.h[7]*y+c.h[8];
 if(!c.calibrated || !finite(x)||!finite(y)||!finite(d)||fabsf(d)<1e-5f)return false;
 u=(c.h[0]*x+c.h[1]*y+c.h[2])/d;v=(c.h[3]*x+c.h[4]*y+c.h[5])/d;
 return finite(u)&&finite(v)&&u>=-.001f&&v>=-.001f&&u<=1.001f&&v<=1.001f;
}
// Corners are clicked clockwise: far-left, far-right, near-right, near-left.
// Their image orientation may rotate; strict convexity rejects crossing edges.
inline bool calibrate(Config& c,const float* xy) {
 float a[8][9]={};float sign=0,area=0;
 for(unsigned i=0;i<4;i++) {
  float x=xy[2*i],y=xy[2*i+1];unsigned j=(i+1)%4,k=(i+2)%4;
  if(!finite(x)||!finite(y)||x<0||x>1||y<0||y>1)return false;
  float cross=(xy[2*j]-x)*(xy[2*k+1]-xy[2*j+1])-(xy[2*j+1]-y)*(xy[2*k]-xy[2*j]);
  if(fabsf(cross)<.002f || (i && cross*sign<=0))return false;sign=cross;
  area+=x*xy[2*j+1]-xy[2*j]*y;
  float u=(i==1||i==2)?1.f:0.f,v=i>=2?1.f:0.f;
  float* r=a[2*i];r[0]=x;r[1]=y;r[2]=1;r[6]=-u*x;r[7]=-u*y;r[8]=u;
  r=a[2*i+1];r[3]=x;r[4]=y;r[5]=1;r[6]=-v*x;r[7]=-v*y;r[8]=v;
 }
 if(fabsf(area)<.02f)return false;
 for(unsigned col=0;col<8;col++) {
  unsigned pivot=col;for(unsigned r=col+1;r<8;r++)if(fabsf(a[r][col])>fabsf(a[pivot][col]))pivot=r;
  if(fabsf(a[pivot][col])<1e-6f)return false;
  for(unsigned j=col;j<9;j++){float t=a[col][j];a[col][j]=a[pivot][j];a[pivot][j]=t;}
  float d=a[col][col];for(unsigned j=col;j<9;j++)a[col][j]/=d;
  for(unsigned r=0;r<8;r++)if(r!=col){float f=a[r][col];for(unsigned j=col;j<9;j++)a[r][j]-=f*a[col][j];}
 }
 Config candidate=c;candidate.calibrated=true;candidate.h[8]=1;
 for(unsigned i=0;i<8;i++)candidate.h[i]=a[i][8];
 float first_d=candidate.h[6]*xy[0]+candidate.h[7]*xy[1]+1;
 for(unsigned i=0;i<4;i++) {
  float u,v,d=candidate.h[6]*xy[2*i]+candidate.h[7]*xy[2*i+1]+1;
  if(d*first_d<=0 || !map(candidate,xy[2*i],xy[2*i+1],u,v) ||
      fabsf(u-((i==1||i==2)?1.f:0.f))>.003f || fabsf(v-(i>=2?1.f:0.f))>.003f)return false;
 }
 memcpy(candidate.corners,xy,sizeof(candidate.corners));c=candidate;return true;
}
inline bool valid_config(const Config& c) {
 if(c.rotation>3||c.brightness>64||c.leds_per_room<1||c.leds_per_room*WALL_COUNT>32 ||
    c.neutral_us<1000||c.neutral_us>2000||c.open_us<1000||c.open_us>2000||
    c.close_us<1000||c.close_us>2000||c.run_ms<20||c.run_ms>2000)return false;
 for(unsigned i=0;i<ROOM_COUNT;i++) {
  const Room& r=c.rooms[i];
  if(!finite(r.x0)||!finite(r.y0)||!finite(r.x1)||!finite(r.y1)||r.x0<0||r.y0<0||r.x1>1||r.y1>1||r.x0>=r.x1||r.y0>=r.y1)return false;
  for(unsigned j=0;j<i;j++)if(r.x0<c.rooms[j].x1 && c.rooms[j].x0<r.x1 && r.y0<c.rooms[j].y1 && c.rooms[j].y0<r.y1)return false;
 }
 return true;
}
inline int room_at(const Config& c,float x,float y) {
 for(unsigned i=0;i<ROOM_COUNT;i++){const Room&r=c.rooms[i];
  if(x>=r.x0&&y>=r.y0&&(x<r.x1||(r.x1==1&&x<=1))&&(y<r.y1||(r.y1==1&&y<=1)))return i;}
 return -1;
}
inline unsigned matrix_index(const Config& c,unsigned x,unsigned y) {
 for(unsigned r=0;r<c.rotation;r++){unsigned t=x;x=15-y;y=t;}
 return y*16+((c.serpentine && (y&1))?15-x:x);
}
inline unsigned cell_axis(float p){return p>=1?15:(p<=0?0:(unsigned)(p*16));}
struct State {
 bool valid,have_observation;float x,y;unsigned track,risk;
 uint32_t observed_ms,candidate_ms,light_ms[ROOM_COUNT];
 int room,candidate;bool lit[ROOM_COUNT];
 uint8_t trail[256];uint32_t trail_ms[256];unsigned prev_cell;
};
inline void reset(State& s){memset(&s,0,sizeof(s));s.room=s.candidate=-1;s.prev_cell=256;}
inline bool expire(State& s,uint32_t now) {
 bool changed=false;
 if(s.valid && (uint32_t)(now-s.observed_ms)>STALE_MS){s.valid=false;s.room=s.candidate=-1;s.prev_cell=256;changed=true;}
 for(unsigned i=0;i<ROOM_COUNT;i++)if(s.lit[i] && (!s.valid || s.room!=(int)i) &&
      (uint32_t)(now-s.light_ms[i])>LIGHT_HOLD_MS){s.lit[i]=false;changed=true;}
 for(unsigned i=0;i<256;i++)if(s.trail[i] && (uint32_t)(now-s.trail_ms[i])>=TRAIL_TTL_MS){s.trail[i]=0;changed=true;}
 return changed;
}
inline bool has_trail(const State& s){for(unsigned i=0;i<256;i++)if(s.trail[i])return true;return false;}
inline bool observe(State& s,const Config& c,float image_x,float image_y,unsigned id,unsigned risk,
                    uint32_t capture_age_ms,uint32_t now) {
 expire(s,now);float x,y;
 if(capture_age_ms>STALE_MS || !map(c,image_x,image_y,x,y))return false;
 if(x<0)x=0;if(y<0)y=0;if(x>1)x=1;if(y>1)y=1;
 if(s.valid && s.track==id){x=.75f*x+.25f*s.x;y=.75f*y+.25f*s.y;}
 else{s.room=s.candidate=-1;s.prev_cell=256;}
 s.x=x;s.y=y;s.track=id;s.risk=risk;s.valid=s.have_observation=true;s.observed_ms=now-capture_age_ms;
 unsigned cell=cell_axis(y)*16+cell_axis(x);
 if(s.prev_cell<256) {
  int px=s.prev_cell%16,py=s.prev_cell/16,tx=cell%16,ty=cell/16;
  int dx=abs(tx-px),dy=abs(ty-py),sx=px<tx?1:-1,sy=py<ty?1:-1,err=dx-dy;
  // Join successive samples with side-adjacent cells, including diagonal moves.
  while(px!=tx||py!=ty) {
   unsigned p=py*16+px;s.trail[p]=1;s.trail_ms[p]=s.observed_ms;
   int e2=2*err;
   if(e2>-dy){err-=dy;px+=sx;p=py*16+px;s.trail[p]=1;s.trail_ms[p]=s.observed_ms;}
   if(e2<dx){err+=dx;py+=sy;}
  }
 }
 s.trail[cell]=1;s.trail_ms[cell]=s.observed_ms;s.prev_cell=cell;
 int next=room_at(c,x,y);
 if(next!=s.candidate){s.candidate=next;s.candidate_ms=now;}
 if(s.room<0 || (uint32_t)(now-s.candidate_ms)>=ROOM_HOLD_MS)s.room=next;
 if(s.room>=0){s.lit[s.room]=true;s.light_ms[s.room]=now;}
 return true;
}
inline uint32_t wire_color(const Config& c,unsigned r,unsigned g,unsigned b,bool rgb) {
 r=r*c.brightness/255;g=g*c.brightness/255;b=b*c.brightness/255;
 return rgb?(r<<16)|(g<<8)|b:(g<<16)|(r<<8)|b;
}
inline int nearest_wall(const State& s) {
 if(!s.valid)return -1;
 const float distances[]={s.y,1-s.x,1-s.y,s.x};unsigned wall=0;
 for(unsigned i=1;i<WALL_COUNT;i++)if(distances[i]<distances[wall])wall=i;
 return wall; // Physical strip order: top, right, bottom, left.
}
inline uint32_t pixel(const State& s,const Config& c,unsigned logical,uint32_t now,bool alarm=false) {
 if(logical>=256) {
  unsigned wall=(logical-256)/c.leds_per_room;
  if(alarm)return wall<WALL_COUNT&&now%1000<500?wire_color(c,255,0,0,c.strip_rgb):0;
  return wall<WALL_COUNT&&nearest_wall(s)==(int)wall?wire_color(c,255,180,75,c.strip_rgb):0;
 }
 unsigned x=logical%16,y=logical/16,r=0,g=0,b=0;
 // Keep unvisited cells dark; room outlines are drawn on the web map only.
 uint32_t age=now-s.trail_ms[logical];
 if(s.trail[logical] && age<TRAIL_TTL_MS){
  if(age<TRAIL_STEP_MS)g=150;
  else if(age<2*TRAIL_STEP_MS)b=150;
  else r=150;
 }
 if(s.valid && x==cell_axis(s.x) && y==cell_axis(s.y)) {
  r=s.risk?255:0;g=s.risk==2?0:255;b=0;
 }
 return wire_color(c,r,g,b,c.matrix_rgb);
}
}
