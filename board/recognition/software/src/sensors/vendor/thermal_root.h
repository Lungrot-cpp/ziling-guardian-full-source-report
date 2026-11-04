#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "thermal_root_table.h"
static inline float thermal_fourth_root(float value) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));
    if(bits<0x00800000u || bits>=0x7f800000u)return sqrtf(sqrtf(value));
    int exponent=(int)(bits>>23)-127;
    int quotient=exponent/4,remainder=exponent%4;
    if(remainder<0){remainder+=4;quotient--;}
    unsigned mantissa=bits&0x7fffffu,index=mantissa>>14,fraction=mantissa&16383;
    uint32_t low=thermal_root_table[remainder][index],high=thermal_root_table[remainder][index+1];
    bits=(uint32_t)((int32_t)(low+(((high-low)*fraction+8192)>>14))+quotient*8388608);
    float root;memcpy(&root,&bits,sizeof(root));
    return root;
}
