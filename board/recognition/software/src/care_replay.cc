#include "care_replay.h"
#include <stdio.h>
#include <string.h>
extern "C" uint32_t sys_now(void);
namespace {
const unsigned W=96,H=54,N=64,BYTES=W*H*2;
uint8_t frames[N][BYTES],bmp[66+BYTES];
unsigned ids[N],stamps[N],head,count,clip_id;
uint32_t trigger_us,deadline,last_us;
bool triggered,ready,previous_fall;
char json[4800];
void word(unsigned at,uint32_t v){memcpy(bmp+at,&v,4);}
}
void replay_reset(){head=count=0;triggered=ready=false;last_us=0;}
void replay_poll(){if(triggered&&!ready&&(int32_t)(sys_now()-deadline)>=0)ready=true;}
void replay_record(const uint8_t* rgb,unsigned frame,uint32_t capture_us,bool fall) {
 replay_poll();bool edge=fall&&!previous_fall;previous_fall=fall;
 if(ready)return;
 if(edge&&!triggered){
  while(count && (uint32_t)(capture_us-stamps[(head+N-count)%N])>8000000)count--;
  triggered=true;trigger_us=capture_us;deadline=sys_now()+5000;clip_id++;
 }
 if(last_us && (uint32_t)(capture_us-last_us)<250000)return;
 if(!triggered)while(count && (uint32_t)(capture_us-stamps[(head+N-count)%N])>8000000)count--;
 last_us=capture_us;
 uint8_t* out=frames[head];
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
  const uint8_t* p=rgb+(((y*96+48)/54)*W+x)*3;
  uint16_t c=((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3);memcpy(out+(y*W+x)*2,&c,2);
 }
 ids[head]=frame;stamps[head]=capture_us;head=(head+1)%N;if(count<N)count++;
}
const char* replay_json() {
 replay_poll();unsigned n=triggered?count:0,at=0;
 int len=snprintf(json,sizeof(json),"{\"clip\":%u,\"ready\":%s,\"recording_post\":%s,\"buffered\":%u,\"count\":%u,\"width\":96,\"height\":54,\"persistent\":false,\"trigger_us\":%u,\"frames\":[",
   clip_id,ready?"true":"false",triggered&&!ready?"true":"false",count,n,trigger_us);
 if(len<0)return "{}";at=len;
 for(unsigned i=0;i<n;i++) {
  unsigned slot=(head+N-count+i)%N;
  len=snprintf(json+at,sizeof(json)-at,"%s{\"index\":%u,\"frame\":%u,\"capture_us\":%u}",i?",":"",i,ids[slot],stamps[slot]);
  if(len<0||(unsigned)len>=sizeof(json)-at)return "{\"error\":\"replay metadata overflow\"}";at+=len;
 }
 snprintf(json+at,sizeof(json)-at,"]}");return json;
}
const uint8_t* replay_bmp(unsigned clip,unsigned index,unsigned* size) {
 replay_poll();if(!ready||clip!=clip_id||index>=count)return nullptr;
 unsigned slot=(head+N-count+index)%N;
 memset(bmp,0,66);bmp[0]='B';bmp[1]='M';word(2,sizeof(bmp));word(10,66);word(14,40);
 word(18,W);word(22,(uint32_t)-(int)H);bmp[26]=1;bmp[28]=16;word(30,3);word(34,BYTES);
 word(54,0xf800);word(58,0x07e0);word(62,0x001f);memcpy(bmp+66,frames[slot],BYTES);*size=sizeof(bmp);return bmp;
}
