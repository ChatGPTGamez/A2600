#pragma once
#include "util/common.h"

enum {
    FB_W = 160,
    FB_H = 192,

    CC_PER_SCANLINE = 228,
    SCANLINES_PER_FRAME = 262,

    VISIBLE_Y_START = 40,
    VISIBLE_Y_END   = VISIBLE_Y_START + FB_H,
    VISIBLE_X_START = 68,
    VISIBLE_X_END   = VISIBLE_X_START + FB_W
};

typedef struct TIA {
    int cc_x;
    int scanline;

    u8 VSYNC;
    u8 VBLANK;
    u8 WSYNC;

    u8 COLUPF;
    u8 COLUBK;
    u8 CTRLPF;
    u8 PF0;
    u8 PF1;
    u8 PF2;

    u8 COLUP0;
    u8 COLUP1;
    u8 GRP0;
    u8 GRP1;
    u8 REFP0;
    u8 REFP1;
    u8 NUSIZ0;
    u8 NUSIZ1;

    int p0_x;
    int p1_x;

    u32 fb[FB_W * FB_H];

    bool wsync_stall;
    bool frame_pulse;
    bool clear_next;
} TIA;

void tia_reset(TIA* t);

u32*       tia_fb(TIA* t);
const u32* tia_fb_const(const TIA* t);

// per-colorclock behavior (draw + advance beam)
void tia_tick_colorclock(TIA* t);

// TIA write/read (includes VSYNC edge behavior but does NOT touch CPU stall)
void tia_write_reg(TIA* t, u16 a, u8 v, bool* out_vsync_fell);
u8   tia_read_reg(const TIA* t, u16 a, bool joy_fire);
