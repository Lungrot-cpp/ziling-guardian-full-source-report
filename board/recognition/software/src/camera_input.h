#pragma once
#include <stdint.h>

static const unsigned CAMERA_INPUT_SIZE=96;
static const unsigned CAMERA_INPUT_ROWS=96;
static const unsigned CAMERA_INPUT_TOP=0;
// Full snapshot adaptation selected against 54-row letterboxing on the development validation set.
static inline void prepare_camera_input(const uint8_t* rgb,int8_t* input,const int8_t* lookup) {
  for(unsigned i=0;i<96*96*3;++i) input[i]=lookup[rgb[i]];
}
static inline float camera_y(float value) { return value; }
