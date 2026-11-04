#pragma once
#include <stdint.h>
static inline uint8_t preview_gain(unsigned value,unsigned multiplier) {
  value=value*multiplier>>8;return value>255?255:value;
}
static inline uint16_t preview_rgb565(unsigned top,unsigned bottom) {
  // DDR packs the first Bayer sample in the high byte.
  const unsigned red=preview_gain(bottom&255,0x185);
  const unsigned green=((top&255)+(bottom>>8))/2;
  const unsigned blue=preview_gain(top>>8,0x200);
  return ((red>>3)<<11)|((green>>2)<<5)|(blue>>3);
}
