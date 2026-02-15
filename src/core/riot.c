#include "core/riot.h"
#include <string.h>

void riot_reset(RIOT* r) {
    memset(r, 0, sizeof(*r));
    r->timer_div = 1;
    r->timer_counter = 0;
    r->intim = 0;
    r->underflow = false;
}

void riot_step_cycles(RIOT* r, int cpu_cycles) {
    for (int i = 0; i < cpu_cycles; i++) {
        r->timer_counter++;
        if (r->timer_counter >= r->timer_div) {
            r->timer_counter = 0;
            int prev = r->intim;
            r->intim = (r->intim - 1) & 0xFF;
            if (prev == 0) r->underflow = true;
        }
    }
}

void riot_write_timer(RIOT* r, u16 addr, u8 v) {
    r->underflow = false;
    r->timer_counter = 0;
    r->intim = v;

    switch (addr & 0x03FF) {
        case 0x0294: r->timer_div = 1; break;
        case 0x0295: r->timer_div = 8; break;
        case 0x0296: r->timer_div = 64; break;
        case 0x0297: r->timer_div = 1024; break;
        default: break;
    }
}
