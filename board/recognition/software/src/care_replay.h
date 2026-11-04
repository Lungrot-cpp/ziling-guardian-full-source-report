#pragma once
#include <stdint.h>
void replay_record(const uint8_t* rgb96,unsigned frame,uint32_t capture_us,bool fall);
void replay_poll();
void replay_reset();
const char* replay_json();
const uint8_t* replay_bmp(unsigned clip,unsigned index,unsigned* size);
