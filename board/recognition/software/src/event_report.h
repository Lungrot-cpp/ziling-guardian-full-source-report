#pragma once
#include "event_ledger.h"
void event_report_init();
void event_report_capture(const EventRecord& event,const uint8_t* rgb);
void event_report_poll();
unsigned event_report_json(char* out,unsigned size);
