#include "camera_preview.h"
#include "isp_preview_pixel.h"
#include "soc.h"
#include "JPEGENC.h"
#include "telemetry.h"
#include "sensors/sensors.h"
#include <string.h>

static uint8_t pixels[320*192*3],compressed[16384];
static JPEGENC encoder;
static uint32_t captured_frame;
static uint32_t raw_snapshot[1920*1080/4];
static bool captured_rgb565;
const uint8_t* camera_preview_pixels(uint32_t* frame) {*frame=captured_frame;return pixels;}
static volatile uint32_t* const camera=reinterpret_cast<volatile uint32_t*>(IO_APB_SLAVE_0_INPUT);
static uint32_t read_register(unsigned offset) {
  asm volatile("fence iorw,iorw" ::: "memory");return camera[offset/4];
}
void camera_preview_capture_latest(uint8_t* rgb565) {
  uint32_t latest=0;unsigned writing=read_register(0x28);
  for(unsigned bank=0;bank<3;bank++)if(bank!=writing){uint32_t id=read_register(0x30+bank*4);if((int32_t)(id-latest)>0)latest=id;}
  camera_preview_capture(latest,rgb565);
}
void camera_preview_capture(uint32_t frame,uint8_t* rgb565) {
  captured_frame=0;
  captured_rgb565=rgb565!=nullptr;
  if(!frame || read_register(0x24)!=0x50525631)return;
  unsigned bank=0;
  for(;bank<3;bank++)if(read_register(0x30+bank*4)==frame && read_register(0x28)!=bank)break;
  if(bank==3)return;
  // DMA writes bypass the CPU cache; validate bank ownership again after copying.
  asm volatile(".word 0x500f" ::: "memory");
  // frame_buffer does not override ddr_buffer's 1920-wide bank allocation.
  const uint32_t* bank_words=reinterpret_cast<const uint32_t*>(0x04000000u+bank*(1920*1080*4+512));
  // Own a complete raw frame before the slower sampled ISP reconstruction.
  // This is DDR, not MMIO. Use the optimized bulk copy after cache invalidation;
  // the ownership check below still rejects a frame overtaken by DMA.
  memcpy(raw_snapshot,bank_words,sizeof(raw_snapshot));
  asm volatile("fence iorw,iorw" ::: "memory");
  if(read_register(0x30+bank*4)!=frame || read_register(0x28)==bank)return;
  const uint16_t* raw=reinterpret_cast<const uint16_t*>(raw_snapshot);
  for(unsigned y=0;y<180;y++) {
    for(unsigned x=0;x<320;x++) {
      IspPixel color=isp_preview_pixel(raw,x*6,y*6);
      unsigned out=(y*320+x)*3;
      if(rgb565) {
        const uint16_t value=((color.r>>3)<<11)|((color.g>>2)<<5)|(color.b>>3);
        const unsigned at=(y*320+x)*2;
        rgb565[at]=value&255;rgb565[at+1]=value>>8;
      } else {
        pixels[out+2]=color.r;pixels[out+1]=color.g;pixels[out]=color.b;
      }
    }
  }
  asm volatile("fence iorw,iorw" ::: "memory");
  captured_frame=frame;
}
unsigned camera_preview_encode(uint32_t frame,const uint8_t** jpeg) {
  if(!frame || captured_frame!=frame || captured_rgb565)return 0;
  // The encoder consumes full 16-row blocks, including the final partial block.
  for(unsigned y=180;y<192;y++)for(unsigned x=0;x<960;x++)pixels[y*960+x]=pixels[179*960+x];
  JPEGENCODE block;
  if(encoder.open(compressed,sizeof(compressed))!=JPEGE_SUCCESS ||
     encoder.encodeBegin(&block,320,180,JPEGE_PIXEL_RGB888,JPEGE_SUBSAMPLE_420,JPEGE_Q_MED)!=JPEGE_SUCCESS)return 0;
  while(block.y<180) {
    if(encoder.addMCU(&block,pixels+(block.y*320+block.x)*3,960)!=JPEGE_SUCCESS)return 0;
    sensors_step();telemetry_service();
  }
  unsigned size=encoder.close();
  if(!size || size>sizeof(compressed))return 0;
  *jpeg=compressed;return size;
}
