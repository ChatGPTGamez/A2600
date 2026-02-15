#include "emu/bus.h"
#include "emu/emu.h"
#include "core/cart.h"
#include "core/riot.h"
#include "core/tia.h"

u8 bus_cpu_read(Emu* e, u16 addr) {
    u16 a13 = addr & 0x1FFF;

    if (a13 & 0x1000) {
        return cart_cpu_read(&e->cart, addr);
    }

    if ((a13 & 0x0080) == 0) {
        return tia_read_reg(&e->tia, (u16)(a13 & 0x003F), e->joy_fire);
    }

    if ((a13 & 0x0200) == 0) {
        return e->riot.ram[a13 & 0x007F];
    } else {
        switch (a13 & 0x03FF) {
            case 0x0280: {
                u8 v = 0xFF;
                if (e->joy_right) v &= (u8)~(1<<4);
                if (e->joy_left)  v &= (u8)~(1<<5);
                if (e->joy_down)  v &= (u8)~(1<<6);
                if (e->joy_up)    v &= (u8)~(1<<7);
                return v;
            }
            case 0x0284: return (u8)e->riot.intim;
            case 0x0285: return e->riot.underflow ? 0x80 : 0x00;
            default: return 0x00;
        }
    }
}

void bus_cpu_write(Emu* e, u16 addr, u8 v) {
    u16 a13 = addr & 0x1FFF;

    if (a13 & 0x1000) {
        (void)v;
        return;
    }

    if ((a13 & 0x0080) == 0) {
        bool vsync_fell = false;
        tia_write_reg(&e->tia, (u16)(a13 & 0x003F), v, &vsync_fell);

        if ((a13 & 0x003F) == 0x02) { // WSYNC
            e->cpu.stalled = true;
        }

        if (vsync_fell) {
            e->tia.wsync_stall = false;
            e->cpu.stalled = false;
        }
        return;
    }

    if ((a13 & 0x0200) == 0) {
        e->riot.ram[a13 & 0x007F] = v;
        return;
    } else {
        switch (a13 & 0x03FF) {
            case 0x0294: case 0x0295: case 0x0296: case 0x0297:
                riot_write_timer(&e->riot, a13, v);
                break;
            default: break;
        }
        return;
    }
}
