#include "tracking_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
extern "C" uint32_t sys_now(void);
namespace {
bool requested_multi=false,active_multi=false;
unsigned last_frame;uint32_t last_ms;
struct Point {unsigned short x,y;};
struct History {
    TelemetryTrack track;
    bool visible;
    unsigned begin,count;
    uint32_t last_point_us;
    Point points[32];
};
History histories[16];
char json[24576];
struct Writer {
    unsigned used;bool ok;
    void add(const char* format,...) {
        if(!ok)return;
        va_list a;va_start(a,format);int n=vsnprintf(json+used,sizeof(json)-used,format,a);va_end(a);
        if(n<0||(unsigned)n>=sizeof(json)-used){ok=false;return;}used+=n;
    }
};
}
bool tracking_begin_frame() {
    if(active_multi!=requested_multi){memset(histories,0,sizeof(histories));active_multi=requested_multi;}
    return active_multi;
}
bool tracking_configure(const char* body) {
    if(!strcmp(body,"mode=single")){requested_multi=false;return true;}
    if(!strcmp(body,"mode=multi")){requested_multi=true;return true;}
    return false;
}
void tracking_publish(const TelemetryTrack* tracks,unsigned count,unsigned frame,uint32_t capture_us) {
    if(count>16)count=16;
    last_frame=frame;last_ms=sys_now();
    for(auto& h:histories)h.visible=false;
    for(unsigned i=0;i<count;i++) {
        const auto& t=tracks[i];if(!t.id)continue;
        History* h=nullptr;
        for(auto& candidate:histories)if(candidate.track.id==t.id){h=&candidate;break;}
        if(!h)for(auto& candidate:histories) {
            bool incoming=false;
            for(unsigned j=0;j<count;j++)if(candidate.track.id&&candidate.track.id==tracks[j].id)incoming=true;
            if(!incoming){h=&candidate;*h=History{};break;}
        }
        if(!h)continue;
        h->track=t;h->visible=true;
        if(!active_multi)continue;
        Point p={(unsigned short)((t.x0+t.x1)/2),(unsigned short)t.y1};
        bool append=!h->count||(uint32_t)(capture_us-h->last_point_us)>=1000000;
        if(h->count){const Point& q=h->points[(h->begin+h->count-1)%32];int dx=(int)p.x-q.x,dy=(int)p.y-q.y;append=append||dx*dx+dy*dy>=64;}
        if(append) {
            if(h->count==32){h->begin=(h->begin+1)%32;--h->count;}
            h->points[(h->begin+h->count++)%32]=p;h->last_point_us=capture_us;
        }
    }
}
const char* tracking_json() {
    Writer w={0,true};
    uint32_t age=sys_now()-last_ms;
    w.add("{\"mode\":\"%s\",\"active_mode\":\"%s\",\"algorithm\":\"iou\",\"max_targets\":16,\"frame\":%u,\"age_ms\":%u,\"tracks\":[",requested_multi?"multi":"single",active_multi?"multi":"single",last_frame,age);
    bool comma=false;
    if(age<=2800)for(const auto& h:histories)if(h.visible) {
        const auto& t=h.track;
        w.add("%s{\"id\":%u,\"state\":%u,\"detector\":%u,\"person_confirmed\":%u,\"box\":[%u,%u,%u,%u],\"trail\":[",comma?",":"",t.id,t.state,t.detector,!t.ir_unconfirmed,t.display_x0,t.display_y0,t.display_x1,t.display_y1);comma=true;
        for(unsigned i=0;i<h.count;i++){const auto& p=h.points[(h.begin+i)%32];w.add("%s[%u,%u]",i?",":"",p.x,p.y);}
        w.add("]}");
    }
    w.add("]}");
    return w.ok?json:"{\"error\":\"tracking response overflow\"}";
}
