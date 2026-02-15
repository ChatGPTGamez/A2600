#include "core/tia.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline u32 argb(u8 a, u8 r, u8 g, u8 b) {
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static u32 tia_color_to_argb(u8 col) {
    int hue  = (col >> 4) & 0x0F;
    int luma = (col & 0x0F);

    double h = (double)hue / 16.0 * 2.0 * M_PI;
    double v = (double)luma / 15.0;
    double s = 0.9;

    double r = v * (1.0 + s * cos(h));
    double g = v * (1.0 + s * cos(h - 2.09439510239));
    double b = v * (1.0 + s * cos(h + 2.09439510239));

    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;

    double m = fmax(r, fmax(g, b));
    if (m > 1.0) { r /= m; g /= m; b /= m; }

    int R = (int)lround(r * 255.0);
    int G = (int)lround(g * 255.0);
    int B = (int)lround(b * 255.0);

    if (R < 0) R = 0; if (R > 255) R = 255;
    if (G < 0) G = 0; if (G > 255) G = 255;
    if (B < 0) B = 0; if (B > 255) B = 255;

    return argb(0xFF, (u8)R, (u8)G, (u8)B);
}

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void tia_reset(TIA* t) {
    memset(t, 0, sizeof(*t));
    t->COLUBK = 0x00;
    t->COLUPF = 0x0E;
    t->COLUP0 = 0x1E;
    t->COLUP1 = 0x9E;
    t->p0_x = 20;
    t->p1_x = 120;
    t->frame_pulse = false;
    t->clear_next = true;
}

u32* tia_fb(TIA* t) { return t->fb; }
const u32* tia_fb_const(const TIA* t) { return t->fb; }

static inline bool tia_visible_now(const TIA* t, int* out_x, int* out_y) {
    int y = t->scanline;
    int xcc = t->cc_x;

    if (y >= VISIBLE_Y_START && y < VISIBLE_Y_END &&
        xcc >= VISIBLE_X_START && xcc < VISIBLE_X_END) {
        *out_y = y - VISIBLE_Y_START;
        *out_x = xcc - VISIBLE_X_START;
        return true;
    }
    return false;
}

static inline int tia_player_scale(u8 nusiz) {
    switch (nusiz & 0x07) {
        case 5: return 2;
        case 7: return 4;
        default: return 1;
    }
}

static inline bool tia_player_pixel_on(const TIA* t, int x, int which) {
    const u8 grp   = (which == 0) ? t->GRP0  : t->GRP1;
    const u8 refp  = (which == 0) ? t->REFP0 : t->REFP1;
    const u8 nusiz = (which == 0) ? t->NUSIZ0: t->NUSIZ1;
    const int px   = (which == 0) ? t->p0_x  : t->p1_x;

    int scale = tia_player_scale(nusiz);
    int local = x - px;
    if (local < 0) return false;
    int bit_index = local / scale;
    if (bit_index < 0 || bit_index > 7) return false;

    bool reflect = (refp & 0x08) != 0;
    if (reflect) bit_index = 7 - bit_index;

    int bit = (grp >> (7 - bit_index)) & 1;
    return bit != 0;
}

static inline u32 tia_playfield_color(const TIA* t) {
    if (t->VBLANK & 0x02) return argb(0xFF, 0, 0, 0);

    int x, y;
    if (!tia_visible_now(t, &x, &y)) return 0;

    int group = x / 4;
    int half = (group < 20) ? 0 : 1;
    int g = group % 20;

    bool reflect = (t->CTRLPF & 0x01) != 0;
    if (half == 1 && reflect) g = 19 - g;

    int bit = 0;
    if (g < 4) {
        bit = (t->PF0 >> (7 - g)) & 1;
    } else if (g < 12) {
        int k = g - 4;
        bit = (t->PF1 >> (7 - k)) & 1;
    } else {
        int k = g - 12;
        bit = (t->PF2 >> k) & 1;
    }

    return bit ? tia_color_to_argb(t->COLUPF) : tia_color_to_argb(t->COLUBK);
}

static inline u32 tia_pixel_color(const TIA* t) {
    if (t->VBLANK & 0x02) return argb(0xFF, 0, 0, 0);

    int x, y;
    if (!tia_visible_now(t, &x, &y)) return 0;

    if (tia_player_pixel_on(t, x, 0)) return tia_color_to_argb(t->COLUP0);
    if (tia_player_pixel_on(t, x, 1)) return tia_color_to_argb(t->COLUP1);

    return tia_playfield_color(t);
}

void tia_tick_colorclock(TIA* t) {
    bool draw_ok = ((t->VSYNC & 0x02) == 0);

    int x, y;
    if (draw_ok && tia_visible_now(t, &x, &y)) {
        t->fb[y * FB_W + x] = tia_pixel_color(t);
    }

    t->cc_x++;
    if (t->cc_x >= CC_PER_SCANLINE) {
        t->cc_x = 0;
        t->scanline++;
        if (t->scanline >= SCANLINES_PER_FRAME) t->scanline = 0;
        t->wsync_stall = false;
    }
}

void tia_write_reg(TIA* t, u16 a, u8 v, bool* out_vsync_fell) {
    if (out_vsync_fell) *out_vsync_fell = false;

    switch (a & 0x003F) {
        case 0x00: { // VSYNC
            u8 prev = t->VSYNC;
            t->VSYNC = v;

            bool was = (prev & 0x02) != 0;
            bool now = (v    & 0x02) != 0;

            if (was && !now) {
                t->cc_x = 0;
                t->scanline = 0;
                t->frame_pulse = true;
                t->clear_next = true;
                if (out_vsync_fell) *out_vsync_fell = true;
            }
        } break;

        case 0x01: t->VBLANK = v; break;
        case 0x02: t->WSYNC  = v; t->wsync_stall = true; break;

        case 0x04: t->NUSIZ0 = v; break;
        case 0x05: t->NUSIZ1 = v; break;
        case 0x06: t->COLUP0 = v; break;
        case 0x07: t->COLUP1 = v; break;
        case 0x08: t->COLUPF = v; break;
        case 0x09: t->COLUBK = v; break;
        case 0x0A: t->CTRLPF = v; break;

        case 0x0B: t->REFP0 = v; break;
        case 0x0C: t->REFP1 = v; break;

        case 0x10: {
            int vx = t->cc_x - VISIBLE_X_START;
            t->p0_x = clampi(vx, 0, FB_W - 1);
        } break;
        case 0x11: {
            int vx = t->cc_x - VISIBLE_X_START;
            t->p1_x = clampi(vx, 0, FB_W - 1);
        } break;

        case 0x0D: t->PF0 = v; break;
        case 0x0E: t->PF1 = v; break;
        case 0x0F: t->PF2 = v; break;

        case 0x1B: t->GRP0 = v; break;
        case 0x1C: t->GRP1 = v; break;

        default: break;
    }
}

u8 tia_read_reg(const TIA* t, u16 a, bool joy_fire) {
    (void)t;
    switch (a & 0x003F) {
        case 0x0C:
            return joy_fire ? 0x00 : 0x80;
        default:
            return 0x00;
    }
}
