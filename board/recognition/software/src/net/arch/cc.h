#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x) do {printf x;} while(0)
#define LWIP_PLATFORM_ASSERT(x) do {printf("LWIP_ASSERT: %s\n",x);while(1) {}} while(0)
#define LWIP_RAND() ((uint32_t)rand())
typedef unsigned int sys_prot_t;
