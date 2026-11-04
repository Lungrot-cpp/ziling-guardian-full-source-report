#pragma once
#include <stdint.h>
struct EventRecord { unsigned id,track,state; uint32_t uptime_ms,frame; unsigned detector,rule,score,weighted,x0,y0,x1,y1,quality; };
struct EventLedger {
  struct Target { unsigned id,state; } targets[16];
  EventRecord events[16];
  unsigned sequence,count,overflow;
  void reset() {
    for(auto& target:targets)target={};
    count=overflow=0; // Keep event IDs unique for archived events and pending uploads.
  }
  bool alarm() const {
    if(overflow)return true;
    for(unsigned i=0;i<16;i++)if(targets[i].state==2)return true;
    return false;
  }
  bool observe(unsigned id,unsigned state,uint32_t ms,unsigned frame) {
    if(!id)return false;
    int slot=-1;
    for(unsigned i=0;i<16;i++)if(targets[i].id==id){slot=i;break;}
    if(slot<0)for(unsigned i=0;i<16;i++)if(!targets[i].state){slot=i;break;}
    if(slot<0){overflow++;return false;}
    Target& target=targets[slot];
    unsigned previous=target.id==id?target.state:0;
    if(previous==2 && state==1)return false;
    target.id=id;target.state=state;
    if(previous==state)return false;
    unsigned index=sequence%16;sequence++;
    events[index]={sequence,id,state,ms,frame};
    if(count<16)count++;
    return true;
  }
};
