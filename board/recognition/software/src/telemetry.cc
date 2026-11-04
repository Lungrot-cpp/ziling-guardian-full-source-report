#include "telemetry.h"
#include "camera_preview.h"
#include "bsp.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static char tx[40960];
static unsigned tx_size,tx_pos,position,dropped,sequence,inference_sequence,last_heat_ms,last_preview_ms;
static uint8_t preview[96*54];
static bool overflow;
static SensorReadings sensors;
static uint8_t pictures[16][96*54];
static unsigned picture_events[16],sent_pictures[16],published_event;
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void append(const char* format,...) {
  if(overflow)return;
  va_list args;va_start(args,format);
  int size=vsnprintf(tx+position,sizeof(tx)-position,format,args);va_end(args);
  if(size<0 || (unsigned)size>=sizeof(tx)-position){overflow=true;return;}
  position+=size;
}
static void encoded(const uint8_t* data,unsigned size) {
  for(unsigned i=0;i<size;i+=3) {
    unsigned value=(unsigned)data[i]<<16;
    if(i+1<size)value|=(unsigned)data[i+1]<<8;
    if(i+2<size)value|=data[i+2];
    append("%c%c%c%c",alphabet[(value>>18)&63],alphabet[(value>>12)&63],
           i+1<size?alphabet[(value>>6)&63]:'=',i+2<size?alphabet[value&63]:'=');
  }
}
void telemetry_service(void) {
  unsigned available=uart_writeAvailability(BSP_UART_TERMINAL);
  if(available>128)available=128;
  while(available-- && tx_pos<tx_size)uart_write(BSP_UART_TERMINAL,tx[tx_pos++]);
}
void telemetry_capture(EventRecord& e,const TelemetryTrack& track,const uint8_t* rgb) {
  e.detector=track.detector;e.rule=track.rule;e.score=track.score;e.weighted=track.weighted;
  e.quality=track.quality;e.x0=track.x0;e.y0=track.y0;e.x1=track.x1;e.y1=track.y1;
  unsigned slot=(e.id-1)%16;picture_events[slot]=e.id;
  for(unsigned i=0;i<96*54;i++) {
    unsigned at=((((2*(i/96)+1)*96)/(2*54))*96+i%96)*3;
    pictures[slot][i]=(77u*rgb[at]+150u*rgb[at+1]+29u*rgb[at+2])>>8;
  }
}
void telemetry_publish(unsigned frame,uint32_t capture_us,uint32_t done_ms,unsigned period_ms,unsigned network_ms,
                       const TelemetryTrack* tracks,unsigned count,const EventLedger& ledger,
                       unsigned prepare_ms,unsigned post_ms,unsigned sensor_ms,const uint8_t* rgb,unsigned capture_to_overlay_ms,
                       const DetectionDiagnostics* detection) {
  const unsigned inference=inference_sequence++;
  if(tx_pos<tx_size){dropped++;return;}
  sensors_readings(&sensors);
  position=32;overflow=false;
  append("{\"alarm_latched\":%u,\"v\":1,\"seq\":%u,\"detection_seq\":%u,\"frame\":%u,\"capture_us\":%u,\"done_ms\":%u,\"period_ms\":%u,\"network_ms\":%u,\"tracks\":[",ledger.alarm()?1:0,sequence,inference,frame,capture_us,done_ms,period_ms,network_ms);
  for(unsigned i=0;i<count;i++) {
    const TelemetryTrack& t=tracks[i];
    append("%s{\"id\":%u,\"state\":%u,\"detector\":%u,\"rule\":%u,\"score\":%u,\"weighted\":%u,\"quality\":%u,\"person_confirmed\":%u,\"box\":[%u,%u,%u,%u]}",i?",":"",t.id,t.state,t.detector,t.rule,t.score,t.weighted,t.quality,!t.ir_unconfirmed,t.x0,t.y0,t.x1,t.y1);
  }
  append("],\"events\":[");
  unsigned first=ledger.sequence-ledger.count;const EventRecord* pending_picture=nullptr;
  for(unsigned i=0;i<ledger.count;i++) {
    const EventRecord& e=ledger.events[(first+i)%16];
    append("%s{\"id\":%u,\"track\":%u,\"state\":%u,\"ms\":%u,\"frame\":%u,\"detector\":%u,\"rule\":%u,\"score\":%u,\"weighted\":%u,\"quality\":%u,\"box\":[%u,%u,%u,%u]}",
           i?",":"",e.id,e.track,e.state,e.uptime_ms,e.frame,e.detector,e.rule,e.score,e.weighted,e.quality,e.x0,e.y0,e.x1,e.y1);
    unsigned slot=(e.id-1)%16;
    if(picture_events[slot]==e.id && sent_pictures[slot]!=e.id && (!pending_picture || e.state>pending_picture->state))pending_picture=&e;
  }
  append("],\"sensors\":{\"ambient\":%d,\"humidity\":%d,\"env_valid\":%u,\"env_age_us\":%u,\"heat_valid\":%u,\"oldest_us\":%u,\"newest_us\":%u,\"age_us\":%u,\"gap_us\":%u",sensors.ambient_centi,sensors.humidity_centi,sensors.environment_valid,sensors.environment_age_us,sensors.thermal_valid,sensors.oldest_us,sensors.newest_us,sensors.age_us,sensors.gap_us);
  bool fresh_events=ledger.sequence!=published_event;
  bool send_heat=!fresh_events && (!sequence || (uint32_t)(done_ms-last_heat_ms)>=1000);
  if(send_heat){append(",\"heat_le\":\"");encoded((const uint8_t*)sensors.heat,sizeof(sensors.heat));append("\"");}
  append("},\"capture_to_overlay_ms\":%u",capture_to_overlay_ms);
  append(",\"diagnostics\":{\"prepare_ms\":%u,\"network_ms\":%u,\"post_ms\":%u,\"sensor_ms\":%u,\"period_ms\":%u,\"capture_frames\":%u,\"thermal_frames\":%u,\"overwritten\":%u,\"torn\":%u,\"pair_drops\":%u,\"mlx_errors\":%u,\"sht_errors\":%u,\"tx_dropped\":%u,\"event_overflow\":%u}",prepare_ms,network_ms,post_ms,sensor_ms,period_ms,sensors.capture_frames,sensors.frames,sensors.overwritten,sensors.torn,sensors.pair_drops,sensors.mlx_errors,sensors.sht_errors,dropped,ledger.overflow);
  if(detection) {
    const DetectionDiagnostics& d=*detection;
    append(",\"detection_chain\":{\"v\":1,\"attempt\":%u,\"snapshot_valid\":%u,\"invalid_snapshots\":%u,\"decoded\":%u,\"candidates\":%u,\"mapped\":%u,\"selected_source\":%d,\"tracked\":%u,\"displayed\":%u,\"motion_valid\":%u,\"motion_cells\":%u,\"input_hash\":%u,\"input_min\":%u,\"input_max\":%u,\"boxes\":[",
      d.attempt,d.snapshot_valid,d.invalid_snapshots,d.decoded,d.candidates,d.mapped,d.selected_source,d.tracked,d.displayed,d.motion_valid,d.motion_cells,d.input_hash,d.input_min,d.input_max);
    for(unsigned i=0;i<d.stored;i++) {
      const DiagnosticBox& b=d.boxes[i];
      append("%s{\"source\":%u,\"box_bp\":[%d,%d,%d,%d],\"objectness\":%d,\"score\":%d,\"accepted\":%u,\"mapped\":%d}",
        i?",":"",i,b.bounds[0],b.bounds[1],b.bounds[2],b.bounds[3],b.objectness,b.score,b.accepted,b.mapped);
    }
    append("]}");
  }
  if(fresh_events)pending_picture=nullptr;
  if(pending_picture) {
    const EventRecord& e=*pending_picture;
    append(",\"snapshot\":{\"event_id\":%u,\"frame\":%u,\"gray\":\"",e.id,e.frame);
    encoded(pictures[(e.id-1)%16],96*54);append("\"}");
  }
  bool send_preview=rgb && !fresh_events && !pending_picture && (!last_preview_ms || (uint32_t)(done_ms-last_preview_ms)>=500);
  if(send_preview) {
    const uint8_t* jpeg=nullptr;
    unsigned jpeg_size=camera_preview_encode(frame,&jpeg);
    if(jpeg_size) {
      append(",\"preview\":{\"frame\":%u,\"jpeg\":\"",frame);
      encoded(jpeg,jpeg_size);append("\"}");
    } else {
    for(unsigned i=0;i<96*54;i++) {
      unsigned at=((((2*(i/96)+1)*96)/(2*54))*96+i%96)*3;
      preview[i]=(rgb[at]&0xe0)|((rgb[at+1]>>3)&0x1c)|(rgb[at+2]>>6);
    }
    append(",\"preview\":{\"frame\":%u,\"rgb332\":\"",frame);
    encoded(preview,sizeof(preview));append("\"}");
    }
  }
  append("}");
  if(overflow){dropped++;return;}
  unsigned length=position-32;
  uint32_t crc=0xffffffffu;
  for(unsigned i=32;i<position;i++) {
    crc^=(uint8_t)tx[i];
    for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
  }
  char header[32];int header_size=snprintf(header,sizeof(header),"@V6 %u %08x ",length,crc^0xffffffffu);
  tx_pos=32-header_size;memcpy(tx+tx_pos,header,header_size);tx[position++]='\n';tx_size=position;
  sequence++;if(send_preview)last_preview_ms=done_ms;if(send_heat)last_heat_ms=done_ms;published_event=ledger.sequence;if(pending_picture)sent_pictures[(pending_picture->id-1)%16]=pending_picture->id;
  telemetry_service();
}
