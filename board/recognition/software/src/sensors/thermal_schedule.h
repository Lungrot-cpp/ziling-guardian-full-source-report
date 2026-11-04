#pragma once
#include <stdint.h>
static inline unsigned thermal_work_due(uint32_t t,uint32_t completed,unsigned alert,unsigned pending,unsigned submask) {
    // Finish a started pair; only idle background work waits for the next second.
    return alert || pending || submask || (uint32_t)(t-completed)>=1000000u;
}
