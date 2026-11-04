#pragma once
#include <stdint.h>
#include "isp_gamma_lut.h"

// Reconstruct sampled preview pixels from the immutable Bayer DDR bank.
// Use the ISP's colour arithmetic; HDMI/NN pixels come from the hardware pipeline.
// The raw pointer is the CPU-owned snapshot, so repeated samples are not volatile.
struct IspPixel {int r,g,b;};
static inline int isp_clip(int v) {return v<0?0:v>255?255:v;}
static inline int isp_raw(const uint16_t* raw,int x,int y) {
  x=x<0?0:x>1919?1919:x;y=y<0?0:y>1079?1079:y;
  unsigned word=raw[(y*960+x/2)^7];
  int value=(x&1)?word&255:word>>8;
  return value>17?value-17:0;
}
static inline IspPixel isp_demosaic(const uint16_t* raw,int x,int y) {
  int c=isp_raw(raw,x,y),h=(isp_raw(raw,x-1,y)+isp_raw(raw,x+1,y))/2;
  int v=(isp_raw(raw,x,y-1)+isp_raw(raw,x,y+1))/2;
  int cross=(isp_raw(raw,x-1,y)+isp_raw(raw,x+1,y)+isp_raw(raw,x,y-1)+isp_raw(raw,x,y+1))/4;
  int diagonal=(isp_raw(raw,x-1,y-1)+isp_raw(raw,x+1,y-1)+isp_raw(raw,x-1,y+1)+isp_raw(raw,x+1,y+1))/4;
  IspPixel p;
  if(!(y&1))p=(x&1)?IspPixel{v,c,h}:IspPixel{diagonal,cross,c};
  else p=(x&1)?IspPixel{c,cross,diagonal}:IspPixel{h,c,v};
  int rr=(p.r*24896+4096)>>13,gg=p.g*2,bb=p.b*4;
  p.r=isp_gamma_lut[rr>511?511:rr];
  p.g=isp_gamma_lut[gg>511?511:gg];
  p.b=isp_gamma_lut[bb>511?511:bb];
  return p;
}
static inline int isp_luma(IspPixel p) {return (54*p.r+183*p.g+18*p.b+128)>>8;}
static inline IspPixel isp_colour(IspPixel p) {
  IspPixel c={isp_clip((16885*p.r+3851*p.g-4352*p.b+8192)>>14),
              isp_clip((-1126*p.r+15054*p.g+2456*p.b+8192)>>14),
              isp_clip((4491*p.r-3344*p.g+15237*p.b+8192)>>14)};
  int y=(77*c.r+150*c.g+29*c.b+128)>>8;
  int gain=y<=32?8192:y>=96?21299:8192+((13107*(y-32)+32)>>6);
  int* channels[]={&c.r,&c.g,&c.b};
  for(unsigned i=0;i<3;i++) {
    int delta=*channels[i]-y,mag=delta<0?-delta:delta;
    int amount=(mag*gain+8192)>>14;
    *channels[i]=isp_clip(y+(delta<0?-amount:amount));
  }
  return c;
}
static inline IspPixel isp_preview_pixel(const uint16_t* raw,int x,int y) {
  int horizontal[3];IspPixel center={0,0,0};int center_y=0;
  for(int row=-1;row<=1;row++) {
    IspPixel left=isp_demosaic(raw,x-1,y+row);
    IspPixel middle=isp_demosaic(raw,x,y+row);
    IspPixel right=isp_demosaic(raw,x+1,y+row);
    horizontal[row+1]=(isp_luma(left)+2*isp_luma(middle)+isp_luma(right)+2)>>2;
    if(row==0){center=middle;center_y=isp_luma(middle);}
  }
  int blur=(horizontal[0]+2*horizontal[1]+horizontal[2]+2)>>2;
  int delta=center_y-blur,mag=delta<0?-delta:delta;
  int amount=(mag*29999+8192)>>14;if(delta<0)amount=-amount;
  center.r=isp_clip(center.r+amount);center.g=isp_clip(center.g+amount);center.b=isp_clip(center.b+amount);
  return isp_colour(center);
}
