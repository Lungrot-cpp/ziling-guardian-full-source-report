#include "telemetry.cc"
#include <string.h>
#include <stddef.h>

extern "C" void* _sbrk(ptrdiff_t size) {
  static char heap[8192];static unsigned used;
  if(size<0 || used+size>sizeof(heap))return (void*)-1;
  void* p=heap+used;used+=size;return p;
}

extern "C" void sensors_readings(SensorReadings* out) { memset(out,0,sizeof(*out)); }
unsigned camera_preview_encode(uint32_t,const uint8_t**) { return 0; }

int main() {
  volatile unsigned* control=(volatile unsigned*)0x10000000;
  const unsigned mode=control[0];
  DetectionDiagnostics d={};d.attempt=11;d.snapshot_valid=mode!=0;d.invalid_snapshots=mode==0?3:2;
  d.selected_source=-1;d.motion_valid=1;d.motion_cells=17;d.input_hash=0xfedcba98u;d.input_min=3;d.input_max=250;
  box boxes[17]={};float scores[17];
  for(unsigned i=0;i<17;i++) {
    scores[i]=.5f+i*.01f;
    boxes[i]={.1f,.2f,.3f,.9f,.9f,&scores[i],0};
  }
  box original[17];memcpy(original,boxes,sizeof(boxes));
  if(mode>=2) {
    diagnostic_decode(d,boxes,mode==4?17:2);
    if(memcmp(original,boxes,sizeof(boxes)))return 1;
    d.candidates=d.decoded;d.mapped=d.decoded;d.boxes[0].accepted=1;d.boxes[0].mapped=0;
    if(mode==2 || mode==4){d.selected_source=0;d.tracked=1;d.displayed=1;}
  }
  if(mode==5) {
    boxes[0].x_min=NAN;boxes[0].y_min=INFINITY;boxes[0].objectness=-INFINITY;
    boxes[0].x_max=1000000;diagnostic_decode(d,boxes,1);
  }
  TelemetryTrack track={7,0,500,0,0,0,192,216,576,972,0,192,216,576,972};
  EventLedger ledger={};
  telemetry_publish(30,1000000,1100,350,224,&track,d.displayed,ledger,1,2,3,nullptr,345,mode==6?nullptr:&d);
  if(mode==7) {
    const unsigned size=tx_size;char original_tx[40960];memcpy(original_tx,tx,size);
    telemetry_publish(31,1000100,1101,350,224,nullptr,0,ledger,0,0,0,nullptr,0,&d);
    if(dropped!=1 || tx_size!=size || memcmp(original_tx,tx,size))return 2;
  }
  control[2]=(unsigned)(tx+tx_pos);control[3]=tx_size-tx_pos;
  control[1]=0x600d;asm volatile("ebreak");return 0;
}
