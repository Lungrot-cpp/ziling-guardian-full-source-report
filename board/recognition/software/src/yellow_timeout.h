#pragma once
#include <stdint.h>

struct YellowTimeoutState {
    unsigned id;
    bool active;
    uint64_t since, last;
};

// Independent of motion scores and thermal availability; follow the same track.
inline bool yellow_timeout_alarm(YellowTimeoutState& s, unsigned id, bool yellow,
                                 uint64_t now, uint64_t hz) {
    if (!yellow || !id) { s=YellowTimeoutState{}; return false; }
    // Keep the existing 2.8-second track gap tolerance for a missed detection.
    if (!s.active || s.id!=id || now<s.last || now-s.last>hz*28/10) {
        s.id=id;s.active=true;s.since=now;
    }
    s.last=now;
    return now-s.since>=hz*10;
}
