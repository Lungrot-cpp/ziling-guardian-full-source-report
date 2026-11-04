#include "onboard_http.h"
#include "camera_preview.h"
#include "care_demo.h"
#ifdef EVENT_REPORT_CHECK
#include "event_report.h"
#endif
#include <string.h>
static uint8_t pixels[320*180*3];
const uint8_t* camera_preview_pixels(uint32_t* frame){*frame=7;return pixels;}
void camera_preview_capture_latest(uint8_t* out){if(out)for(unsigned i=0;i<320*180;i++){
 unsigned p=i*3;uint16_t c=((pixels[p+2]>>3)<<11)|((pixels[p+1]>>2)<<5)|(pixels[p]>>3);
 out[i*2]=c;out[i*2+1]=c>>8;
}}
extern "C" void sensors_readings(SensorReadings* s){memset(s,0,sizeof(*s));s->ambient_centi=2350;s->humidity_centi=4560;s->environment_valid=s->thermal_valid=1;for(auto& t:s->heat)t=2600;}
int main(){
  for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=i%251;
  onboard_http_init();care_init();EventLedger ledger={};ledger.observe(1,2,1000,7);
  TelemetryTrack track={1,2,800,850,830,0,60,60,600,900,0,60,60,600,900};
  onboard_http_update(7,1000000,1100,400,&track,1,ledger);
  for(;;){
#ifdef EVENT_REPORT_CHECK
    if(*(volatile unsigned*)0xf8100348==1){
      *(volatile unsigned*)0xf8100348=0;
      static uint8_t rgb[96*96*3];for(unsigned i=0;i<sizeof(rgb);i++)rgb[i]=i%251;
      for(unsigned id=1;id<=18;id++){
        EventRecord e={id,3,id%3,12345+id,77,850,700,780,1,100,80,900,1000,0};event_report_capture(e,rgb);
      }
    }
#endif
    onboard_http_poll();
    if(*(volatile unsigned*)0xf810034c==1) {
      *(volatile unsigned*)0xf810034c=0;track.state=2;ledger.observe(2,2,2000,8);
      onboard_http_update(8,2000000,2100,400,&track,1,ledger);
    }
    if(monitor_reset_completed!=monitor_reset_requested) {
      ledger.reset();care_action("cmd=reset");monitor_reset_completed=monitor_reset_requested;
      onboard_http_update(8,2000000,2100,400,nullptr,0,ledger);
    }
    *(volatile unsigned*)0xf8100344=0x1234;
  }
}
