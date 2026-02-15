#pragma once
#include "util/common.h"

typedef struct RIOT {
    u8 ram[128];
    int timer_div;
    int timer_counter;
    int intim;
    bool underflow;
} RIOT;

void riot_reset(RIOT* r);
void riot_step_cycles(RIOT* r, int cpu_cycles);
void riot_write_timer(RIOT* r, u16 addr, u8 v);
