#include "event_report.h"
#include <stdio.h>
#include <string.h>
extern "C" {
#include "lwip/tcp.h"
#include "lwip/sys.h"
}

static const unsigned capacity=16,header_size=68,frame_size=header_size+96*54*3;
static uint8_t queue[capacity][frame_size],session[16],incoming[20];
static unsigned head,count,dropped,confirmed,reconnections,last_id,offset,used;
static uint32_t attempted,progress;
static tcp_pcb* connection;
static bool initialized,connected,ready,hello_sent,ping;
static void number(uint8_t* out,uint32_t value){for(unsigned i=0;i<4;i++)out[i]=value>>(24-8*i);}
static uint32_t number(const uint8_t* in){return (uint32_t)in[0]<<24|(uint32_t)in[1]<<16|(uint32_t)in[2]<<8|in[3];}
static void lost(void*,err_t){connection=nullptr;connected=ready=hello_sent=ping=false;offset=used=0;attempted=sys_now();}
static void disconnect(){tcp_pcb* pcb=connection;if(pcb){tcp_arg(pcb,nullptr);tcp_err(pcb,nullptr);tcp_abort(pcb);}lost(nullptr,ERR_ABRT);}
static err_t received(void*,tcp_pcb* pcb,pbuf* packet,err_t error) {
  if(!packet){disconnect();return ERR_ABRT;}
  if(error!=ERR_OK){pbuf_free(packet);return error;}
  tcp_recved(pcb,packet->tot_len);
  unsigned expected=ready?8:20;
  if(used+packet->tot_len>expected){pbuf_free(packet);disconnect();return ERR_ABRT;}
  used+=pbuf_copy_partial(packet,incoming+used,packet->tot_len,0);pbuf_free(packet);
  if(used!=expected)return ERR_OK;
  used=0;
  if(!ready) {
    const uint8_t empty[16]={};
    if(memcmp(incoming,"EVS2",4)||!memcmp(incoming+4,empty,16)||
       (memcmp(session,empty,16)&&memcmp(session,incoming+4,16))){disconnect();return ERR_ABRT;}
    memcpy(session,incoming+4,16);ready=true;
  } else {
    unsigned id=number(incoming+4);
    if(memcmp(incoming,"EVA2",4)||(!ping&&(!count||offset!=frame_size||id!=number(queue[head]+4)))||(ping&&id)) {
      disconnect();return ERR_ABRT;
    }
    if(!ping){last_id=id;confirmed++;head=(head+1)%capacity;count--;offset=0;}
    ping=false;
  }
  progress=sys_now();return ERR_OK;
}
static err_t established(void*,tcp_pcb*,err_t error){if(error!=ERR_OK){disconnect();return ERR_ABRT;}connected=true;progress=sys_now();return ERR_OK;}
void event_report_init(){initialized=true;attempted=sys_now()-1000;}
void event_report_capture(const EventRecord& e,const uint8_t* rgb) {
  if(count==capacity){dropped++;return;}
  uint8_t* out=queue[(head+count)%capacity];memcpy(out,"EVT3",4);
  const unsigned fields[]={e.id,e.track,e.state,e.uptime_ms,e.frame,e.detector,e.rule,e.score,e.weighted,e.x0,e.y0,e.x1,e.y1,e.quality,96,54};
  for(unsigned i=0;i<16;i++)number(out+4+i*4,fields[i]);
  for(unsigned i=0;i<96*54;i++) {
    unsigned at=((((2*(i/96)+1)*96)/(2*54))*96+i%96)*3;
    memcpy(out+header_size+i*3,rgb+at,3);
  }
  // shortcut: pending events live in DDR; power loss or more than 16 unconfirmed transitions can lose events.
  count++;
}
void event_report_poll() {
  if(!initialized)return;
  uint32_t now=sys_now();
  if(connection&&(uint32_t)(now-progress)>15000){disconnect();return;}
  if(!connection) {
    if((uint32_t)(now-attempted)<1000)return;
    attempted=progress=now;connection=tcp_new();if(!connection)return;
    reconnections++;tcp_recv(connection,received);tcp_err(connection,lost);tcp_nagle_disable(connection);
    ip_addr_t peer;IP_ADDR4(&peer,192,168,0,3);
    if(tcp_connect(connection,&peer,8091,established)!=ERR_OK)disconnect();
    return;
  }
  if(!connected)return;
  if(!hello_sent) {
    uint8_t hello[20];memcpy(hello,"EVH2",4);memcpy(hello+4,session,16);
    if(tcp_write(connection,hello,sizeof(hello),TCP_WRITE_FLAG_COPY)==ERR_OK){hello_sent=true;tcp_output(connection);}return;
  }
  if(!ready||ping)return;
  if(count) {
    unsigned n=frame_size-offset;if(n>1460)n=1460;if(n>tcp_sndbuf(connection))n=tcp_sndbuf(connection);
    if(n&&tcp_write(connection,queue[head]+offset,n,TCP_WRITE_FLAG_COPY)==ERR_OK){offset+=n;tcp_output(connection);}
  } else if((uint32_t)(now-progress)>=5000) {
    const uint8_t heartbeat[8]={'E','V','P','2',0,0,0,0};
    if(tcp_write(connection,heartbeat,sizeof(heartbeat),TCP_WRITE_FLAG_COPY)==ERR_OK){ping=true;tcp_output(connection);}
  }
}
unsigned event_report_json(char* out,unsigned size) {
  char token[33];for(unsigned i=0;i<16;i++)sprintf(token+i*2,"%02x",session[i]);
  int n=snprintf(out,size,"{\"server\":\"192.168.0.3:8091\",\"session\":\"%s\",\"connected\":%s,\"queued\":%u,\"capacity\":16,\"confirmed\":%u,\"dropped\":%u,\"connections\":%u,\"last_confirmed_id\":%u,\"pending_volatile\":true}",token,ready?"true":"false",count,confirmed,dropped,reconnections,last_id);
  return n>=0&&(unsigned)n<size?n:0;
}
