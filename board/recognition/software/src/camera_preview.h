#pragma once
#include <stdint.h>
void camera_preview_capture(uint32_t frame,uint8_t* rgb565=nullptr);
void camera_preview_capture_latest(uint8_t* rgb565=nullptr);
unsigned camera_preview_encode(uint32_t frame,const uint8_t** jpeg);
const uint8_t* camera_preview_pixels(uint32_t* frame);
