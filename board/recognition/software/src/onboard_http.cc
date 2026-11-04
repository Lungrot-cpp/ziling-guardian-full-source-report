#include "onboard_http.h"
#include "camera_preview.h"
#include "bsp.h"
#include "board_page.h"
#include "board_assets.h"
#include "care_demo.h"
#include "care_replay.h"
#include "tracking_runtime.h"
#include "power_policy.h"
#include "event_report.h"
#include "person_fusion.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
extern "C" {
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
}

static volatile uint32_t* const nic=(volatile uint32_t*)0xf810c000;
static netif interface;
static bool enabled,busy;
bool infrared_assist_enabled=true,infrared_assist_active=true;
unsigned monitor_reset_requested,monitor_reset_completed;
static unsigned rxslot,requests,image_frame,image_capture;
static const unsigned image_bytes=66+320*180*2;
static uint8_t image_storage[2][image_bytes];
static uint8_t* image=image_storage[0];
static uint8_t* image_spare=image_storage[1];
static char status_json[16384]="{}",events_json[8192]="{\"events\":[],\"volatile\":true}";
static uint32_t txwords[384],rxwords[384];
static TelemetryTrack image_tracks[16];
static unsigned image_count,image_detection_ms;
static uint32_t last_image_render_ms;
static void refresh_image(uint32_t capture_us);
extern "C" uint32_t sys_now(void) {return clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000);}
static uint32_t reg(unsigned offset) {asm volatile("fence iorw,iorw" ::: "memory");return nic[offset/4];}
static void put(unsigned offset,uint32_t value) {nic[offset/4]=value;asm volatile("fence iorw,iorw" ::: "memory");}
static err_t output(netif*,pbuf* packet) {
  if(packet->tot_len<14 || packet->tot_len>1514)return ERR_IF;
  uint32_t start=sys_now();
  while(reg(4)&1)if((uint32_t)(sys_now()-start)>2)return ERR_MEM;
  txwords[(packet->tot_len-1)/4]=0;pbuf_copy_partial(packet,txwords,packet->tot_len,0);
  for(unsigned i=0;i<(packet->tot_len+3u)/4;i++)nic[0x200+i]=txwords[i];
  put(20,packet->tot_len);return ERR_OK;
}
static err_t net_init(netif* net) {
  net->name[0]='e';net->name[1]='n';net->hostname="v6-ti60";
  const uint8_t mac[6]={0,10,53,1,254,192};memcpy(net->hwaddr,mac,6);net->hwaddr_len=6;
  net->mtu=1500;net->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_LINK_UP;
  net->output=etharp_output;net->linkoutput=output;return ERR_OK;
}
struct Client {tcp_pcb* pcb;char request[1024];unsigned used,queued,acked,size,last;char* response;};
static Client clients[4];
static void forgotten(void* arg,err_t) {Client* c=(Client*)arg;free(c->response);memset(c,0,sizeof(*c));}
static void send_more(Client* c) {
  while(c->queued<c->size) {
    unsigned n=c->size-c->queued;if(n>1460)n=1460;if(n>tcp_sndbuf(c->pcb))n=tcp_sndbuf(c->pcb);if(!n)break;
    // The response stays immutable until every byte is acknowledged.
    if(tcp_write(c->pcb,c->response+c->queued,n,0)!=ERR_OK)break;
    c->queued+=n;
  }
  tcp_output(c->pcb);
}
static err_t finish(Client* c) {
  tcp_arg(c->pcb,nullptr);tcp_recv(c->pcb,nullptr);tcp_sent(c->pcb,nullptr);tcp_err(c->pcb,nullptr);tcp_poll(c->pcb,nullptr,0);
  bool aborted=tcp_close(c->pcb)!=ERR_OK;if(aborted)tcp_abort(c->pcb);
  forgotten(c,ERR_OK);return aborted?ERR_ABRT:ERR_OK;
}
static err_t sent(void* arg,tcp_pcb*,u16_t n) {
  Client* c=(Client*)arg;c->last=sys_now();c->acked+=n;
  if(c->acked>=c->size)return finish(c);send_more(c);return ERR_OK;
}
static void reply(Client* c,const char* code,const char* type,const void* body,unsigned size,const char* extra="") {
  char header[320];int n=snprintf(header,sizeof(header),"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n%s\r\n",code,type,size,extra);
  if(n<0 || (unsigned)n>=sizeof(header))return;
  c->response=(char*)malloc(n+size);if(!c->response)return;
  memcpy(c->response,header,n);memcpy(c->response+n,body,size);c->size=n+size;requests++;send_more(c);
}
static err_t receive(void* arg,tcp_pcb* pcb,pbuf* packet,err_t error) {
  Client* c=(Client*)arg;
  if(!packet)return c->response?ERR_OK:finish(c);
  if(error!=ERR_OK){pbuf_free(packet);return error;}
  tcp_recved(pcb,packet->tot_len);c->last=sys_now();
  if(c->response){pbuf_free(packet);return ERR_OK;}
  if(c->used+packet->tot_len>=sizeof(c->request)) {
    pbuf_free(packet);reply(c,"431 Request Header Fields Too Large","text/plain","Request too large",17);return ERR_OK;
  }
  c->used+=pbuf_copy_partial(packet,c->request+c->used,packet->tot_len,0);pbuf_free(packet);c->request[c->used]=0;
  char* header_end=strstr(c->request,"\r\n\r\n");if(!header_end)return ERR_OK;
  char path[128],version[16],method[8];
  if(sscanf(c->request,"%7s %127s %15s",method,path,version)!=3 || strncmp(version,"HTTP/1.",7)) {
    reply(c,"405 Method Not Allowed","text/plain","GET required",12,"Allow: GET\r\n");return ERR_OK;
  }
  if(!strcmp(method,"POST") && (!strcmp(path,"/api/care/config")||!strcmp(path,"/api/care/action")||!strcmp(path,"/api/replay/reset")||!strcmp(path,"/api/tracking")||!strcmp(path,"/api/power")||!strcmp(path,"/api/infrared")||!strcmp(path,"/api/monitor/reset"))) {
    unsigned body_length=0;bool length_seen=false;
    for(char* line=strstr(c->request,"\r\n")+2;line<header_end;) {
      char* next=strstr(line,"\r\n");if(!next)break;
      if(!strncasecmp(line,"Transfer-Encoding:",18)){reply(c,"400 Bad Request","text/plain","Unsupported encoding",20);return ERR_OK;}
      if(!strncasecmp(line,"Content-Length:",15)) {
        char* end;unsigned long n=strtoul(line+15,&end,10);
        if(length_seen||end==line+15||end!=next||n>700){reply(c,"400 Bad Request","text/plain","Invalid length",14);return ERR_OK;}
        body_length=n;length_seen=true;
      }line=next+2;
    }
    if(!length_seen){reply(c,"411 Length Required","text/plain","Length required",15);return ERR_OK;}
    unsigned header_length=header_end+4-c->request;
    if(c->used<header_length+body_length)return ERR_OK;
    c->request[header_length+body_length]=0;
    if(!strcmp(path,"/api/monitor/reset")) {
      if(body_length){reply(c,"409 Conflict","application/json","{\"ok\":false}",12);return ERR_OK;}
      ++monitor_reset_requested;power_policy.wake(WAKE_MANUAL,sys_now());
      char result[80];unsigned size=snprintf(result,sizeof(result),"{\"ok\":true,\"reset_id\":%u}",monitor_reset_requested);
      reply(c,"200 OK","application/json",result,size);return ERR_OK;
    }
    bool ok;
    if(!strcmp(path,"/api/infrared")) {
      const char* body=c->request+header_length;
      ok=body_length==9 && (!strcmp(body,"enabled=0") || !strcmp(body,"enabled=1"));
      if(ok){infrared_assist_enabled=body[8]=='1';power_policy.wake(WAKE_MANUAL,sys_now());}
    }
    else if(!strcmp(path,"/api/power"))ok=power_policy.configure(c->request+header_length,sys_now());
    else if(!strcmp(path,"/api/tracking"))ok=tracking_configure(c->request+header_length);
    else if(!strcmp(path,"/api/replay/reset")){replay_reset();ok=true;}
    else ok=!strcmp(path,"/api/care/config")?care_configure(c->request+header_length):care_action(c->request+header_length);
    const char* result=ok?"{\"ok\":true}":"{\"ok\":false}";
    reply(c,ok?"200 OK":"409 Conflict","application/json",result,strlen(result));return ERR_OK;
  }
  if(strcmp(method,"GET")){reply(c,"405 Method Not Allowed","text/plain","GET required",12,"Allow: GET\r\n");return ERR_OK;}
  if(!strncmp(path,"/replay.bmp?",12)) {
    unsigned clip,index;char tail;
    if(sscanf(path,"/replay.bmp?clip=%u&index=%u%c",&clip,&index,&tail)!=2){reply(c,"400 Bad Request","text/plain","Invalid frame",13);return ERR_OK;}
    unsigned size;const uint8_t* bytes=replay_bmp(clip,index,&size);
    if(bytes)reply(c,"200 OK","image/bmp",bytes,size);else reply(c,"404 Not Found","text/plain","Clip unavailable",16);return ERR_OK;
  }
  if(char* query=strchr(path,'?'))*query=0;
  if(!strcmp(path,"/"))reply(c,"200 OK","text/html; charset=utf-8",board_page,sizeof(board_page)-1);
  else if(const BoardAsset* asset=board_asset(path))reply(c,"200 OK",asset->type,asset->bytes,asset->size,asset->extra);
  else if(!strcmp(path,"/api/power")){char value[768];unsigned size=power_json(value,sizeof(value),sys_now());reply(c,"200 OK","application/json",value,size);}
  else if(!strcmp(path,"/api/infrared")){char value[128];unsigned size=snprintf(value,sizeof(value),"{\"enabled\":%u,\"active\":%u,\"calibrated\":%u}",infrared_assist_enabled,infrared_assist_active,FUSION_CALIBRATED);reply(c,"200 OK","application/json",value,size);}
  else if(!strcmp(path,"/api/report")){char value[512];unsigned size=event_report_json(value,sizeof(value));reply(c,"200 OK","application/json",value,size);}
  else if(!strcmp(path,"/api/status"))reply(c,"200 OK","application/json; charset=utf-8",status_json,strlen(status_json));
  else if(!strcmp(path,"/api/events"))reply(c,"200 OK","application/json; charset=utf-8",events_json,strlen(events_json));
  else if(!strcmp(path,"/api/care")){const char* value=care_json();reply(c,"200 OK","application/json; charset=utf-8",value,strlen(value));}
  else if(!strcmp(path,"/api/tracking")){const char* value=tracking_json();reply(c,"200 OK","application/json; charset=utf-8",value,strlen(value));}
  else if(!strcmp(path,"/api/replay")){const char* value=replay_json();reply(c,"200 OK","application/json; charset=utf-8",value,strlen(value));}
  else if(!strcmp(path,"/frame.bmp")) {
    uint32_t sampled=sys_now();refresh_image(sampled*1000);
    if(!image_frame){reply(c,"503 Service Unavailable","text/plain","Waiting for camera",18);return ERR_OK;}
    char metadata[96];snprintf(metadata,sizeof(metadata),"X-Frame-Id: %u\r\nX-Capture-Ms: %u\r\n",image_frame,image_capture/1000);
    reply(c,"200 OK","image/bmp",image,image_bytes,metadata);
  } else reply(c,"404 Not Found","text/plain","Not available",13);
  return ERR_OK;
}
static err_t poll(void* arg,tcp_pcb*) {
  Client* c=(Client*)arg;
  if((uint32_t)(sys_now()-c->last)>10000)return finish(c);
  if(c->response)send_more(c);return ERR_OK;
}
static err_t accept(void*,tcp_pcb* pcb,err_t error) {
  if(error!=ERR_OK)return error;
  for(Client& c:clients)if(!c.pcb) {
    c={};c.pcb=pcb;c.last=sys_now();tcp_arg(pcb,&c);tcp_recv(pcb,receive);tcp_sent(pcb,sent);tcp_err(pcb,forgotten);tcp_poll(pcb,poll,2);tcp_nagle_disable(pcb);return ERR_OK;
  }
  tcp_abort(pcb);return ERR_ABRT;
}
void onboard_http_init() {
  if(reg(0)!=0x4e494331)return;
  lwip_init();ip4_addr_t ip,mask,gateway;IP4_ADDR(&ip,192,168,0,2);IP4_ADDR(&mask,255,255,255,0);IP4_ADDR(&gateway,192,168,0,1);
  if(!netif_add(&interface,&ip,&mask,&gateway,nullptr,net_init,ethernet_input))return;
  netif_set_default(&interface);netif_set_up(&interface);
  tcp_pcb* pcb=tcp_new();if(!pcb || tcp_bind(pcb,IP_ADDR_ANY,80)!=ERR_OK)return;
  // A debugger reload restarts the CPU without resetting the receive ring.
  for(unsigned slot=0;slot<2;slot++)if(reg(4)&(16u<<slot)){put(8,slot);put(16,1);}
  pcb=tcp_listen_with_backlog(pcb,4);if(!pcb)return;tcp_accept(pcb,accept);enabled=true;
  event_report_init();
  uart_writeStr(BSP_UART_TERMINAL,"ONBOARD_HTTP_READY http://192.168.0.2/ RISC-V/lwIP NIC1\r\n");
}
bool onboard_http_active(){return enabled;}
extern "C" void onboard_http_poll() {
  care_poll();replay_poll();
  if(!enabled || busy)return;busy=true;
  const uint64_t start=clint_getTime(BSP_CLINT);
  for(unsigned budget=0;budget<64 && clint_getTime(BSP_CLINT)-start<SYSTEM_CLINT_HZ/500;budget++) {
    unsigned pending=(reg(4)>>4)&3;
    if(!pending)break;
    for(unsigned skip=0;skip<2 && !(pending&(1u<<rxslot));skip++)rxslot=(rxslot+1)%2;
    put(8,rxslot);unsigned length=reg(12);
    if(length>=14 && length<=1514) {
      for(unsigned i=0;i<(length+3)/4;i++)rxwords[i]=nic[0x400+i];
      put(16,1);rxslot=(rxslot+1)%2;
      pbuf* packet=pbuf_alloc(PBUF_RAW,length,PBUF_POOL);
      if(packet){pbuf_take(packet,rxwords,length);if(interface.input(packet,&interface)!=ERR_OK)pbuf_free(packet);}
    } else {put(16,1);rxslot=(rxslot+1)%2;}
  }
  sys_check_timeouts();event_report_poll();busy=false;
}
struct Json {
  char* text;unsigned size,pos;bool valid;
  void add(const char* format,...) {
    if(!valid)return;va_list args;va_start(args,format);int n=vsnprintf(text+pos,size-pos,format,args);va_end(args);
    if(n<0 || (unsigned)n>=size-pos){valid=false;text[0]=0;}else pos+=n;
  }
};
static void field32(unsigned at,uint32_t value){memcpy(image+at,&value,4);}
static void box_pixel(unsigned x,unsigned y,const uint8_t* color){uint16_t value=((color[2]>>3)<<11)|((color[1]>>2)<<5)|(color[0]>>3);memcpy(image+66+(y*320+x)*2,&value,2);}
static void refresh_image(uint32_t capture_us) {
  // Reuse the immutable preview for closely spaced browser requests. Rendering
  // the same view for several clients must not repeatedly pause inference.
  if(image_frame&&(uint32_t)(sys_now()-last_image_render_ms)<800)return;
  camera_preview_capture_latest(image_spare+66);
  uint32_t preview_frame;camera_preview_pixels(&preview_frame);
  if(preview_frame) {
    // Keep the last complete image when DMA overtakes the bank being copied.
    uint8_t* previous=image;image=image_spare;image_spare=previous;
    memset(image,0,66);image[0]='B';image[1]='M';field32(2,image_bytes);field32(10,66);field32(14,40);field32(18,320);field32(22,(uint32_t)-180);image[26]=1;image[28]=16;field32(30,3);field32(34,320*180*2);field32(54,0xf800);field32(58,0x7e0);field32(62,0x1f);
    image_frame=preview_frame;image_capture=capture_us;
    last_image_render_ms=sys_now();
    const uint8_t colors[3][3]={{74,149,39},{0,160,209},{46,57,222}};
    for(unsigned i=0;i<image_count && (uint32_t)(sys_now()-image_detection_ms)<2000;i++) {
      const auto& t=image_tracks[i];unsigned x0=t.display_x0/6,x1=t.display_x1/6,y0=t.display_y0/6,y1=t.display_y1/6;
      if(x0>x1 || x1>=320 || y0>y1 || y1>=180 || t.state>2 || (t.state==0 && t.ir_unconfirmed))continue;
      for(unsigned x=x0;x<=x1;x++){box_pixel(x,y0,colors[t.state]);box_pixel(x,y1,colors[t.state]);}
      for(unsigned y=y0;y<=y1;y++){box_pixel(x0,y,colors[t.state]);box_pixel(x1,y,colors[t.state]);}
    }
  }
}
void onboard_http_update(unsigned frame,uint32_t capture_us,uint32_t done_ms,unsigned period_ms,const TelemetryTrack* tracks,unsigned count,const EventLedger& ledger) {
  if(!enabled)return;
  image_count=count<16?count:16;memcpy(image_tracks,tracks,image_count*sizeof(*tracks));image_detection_ms=sys_now();
  static SensorReadings s;sensors_readings(&s);
  Json j={status_json,sizeof(status_json),0,true};
  j.add("{\"server\":\"riscv-lwip\",\"ip\":\"192.168.0.2\",\"uptime_ms\":%u,\"frame_id\":%u,\"capture_us\":%u,\"image_frame\":%u,\"image_capture_us\":%u,\"period_ms\":%u,\"alarm_latched\":%u,\"requests\":%u,\"rx_bad\":%u,\"rx_dropped\":%u,\"tracks\":[",done_ms,frame,capture_us,image_frame,image_capture,period_ms,ledger.alarm(),requests,reg(24),reg(28));
  for(unsigned i=0;i<count;i++){const auto& t=tracks[i];j.add("%s{\"id\":%u,\"state\":%u,\"detector\":%u,\"rule\":%u,\"weighted\":%u,\"score\":%u,\"quality\":%u,\"person_confirmed\":%u,\"box\":[%u,%u,%u,%u],\"display_box\":[%u,%u,%u,%u]}",i?",":"",t.id,t.state,t.detector,t.rule,t.weighted,t.score,t.quality,!t.ir_unconfirmed,t.x0,t.y0,t.x1,t.y1,t.display_x0,t.display_y0,t.display_x1,t.display_y1);}
  j.add("],\"sensors\":{\"ambient\":%d,\"humidity\":%d,\"env_valid\":%u,\"thermal_valid\":%u,\"age_us\":%u,\"oldest_us\":%u,\"newest_us\":%u,\"thermal_frames\":%u,\"environment_age_us\":%u,\"gap_us\":%u,\"heat\":[",s.ambient_centi,s.humidity_centi,s.environment_valid,s.thermal_valid,s.age_us,s.oldest_us,s.newest_us,s.frames,s.environment_age_us,s.gap_us);
  for(unsigned i=0;i<768;i++)j.add("%s%d",i?",":"",s.heat[i]);
  char power[768];power_json(power,sizeof(power),done_ms);j.add("]},\"power\":%s,\"infrared_assist\":{\"enabled\":%u,\"active\":%u,\"calibrated\":%u},\"monitor_reset\":{\"requested\":%u,\"completed\":%u}}",power,infrared_assist_enabled,infrared_assist_active,FUSION_CALIBRATED,monitor_reset_requested,monitor_reset_completed);
  if(!j.valid)strcpy(status_json,"{\"error\":\"status overflow\"}");
  Json e={events_json,sizeof(events_json),0,true};e.add("{\"volatile\":true,\"capacity\":16,\"overflow\":%u,\"events\":[",ledger.overflow);
  for(unsigned i=0;i<ledger.count;i++){const auto& v=ledger.events[(ledger.sequence-ledger.count+i)%16];e.add("%s{\"id\":%u,\"track\":%u,\"state\":%u,\"uptime_ms\":%u,\"frame\":%u,\"detector\":%u,\"rule\":%u}",i?",":"",v.id,v.track,v.state,v.uptime_ms,v.frame,v.detector,v.rule);}
  e.add("]}");if(!e.valid)strcpy(events_json,"{\"error\":\"events overflow\"}");
}
