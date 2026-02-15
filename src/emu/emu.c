#include "emu/emu.h"
#include "emu/bus.h"
#include <string.h>
#include <SDL3/SDL.h>

static void system_reset(Emu* e) {
    tia_reset(&e->tia);
    riot_reset(&e->riot);
    cpu6507_reset(&e->cpu, e, (cpu_read_fn)bus_cpu_read, (cpu_write_fn)bus_cpu_write);
    e->reset_request = false;
}

void emu_init(Emu* e) {
    memset(e, 0, sizeof(*e));
    e->has_cart = false;
    e->fatal_error = false;
}

bool emu_load_rom_path(Emu* e, const char* path) {
    emu_unload_rom(e);
    if (!cart_load_path(&e->cart, path)) return false;
    e->has_cart = true;
    e->fatal_error = false;
    system_reset(e);
    return true;
}

void emu_unload_rom(Emu* e) {
    cart_free(&e->cart);
    e->has_cart = false;
}

void emu_request_reset(Emu* e) {
    e->reset_request = true;
}

void emu_set_joy(Emu* e, bool up, bool down, bool left, bool right, bool fire) {
    e->joy_up = up;
    e->joy_down = down;
    e->joy_left = left;
    e->joy_right = right;
    e->joy_fire = fire;
}

static void step_colorclocks(Emu* e, int colorclocks) {
    for (int i = 0; i < colorclocks; i++) {
        tia_tick_colorclock(&e->tia);
        if (e->cpu.stalled && !e->tia.wsync_stall) {
            e->cpu.stalled = false;
        }
    }
}

static void step_cpu_cycles(Emu* e, int cpu_cycles) {
    riot_step_cycles(&e->riot, cpu_cycles);
    step_colorclocks(e, cpu_cycles * 3);
}

void emu_run_until_frame(Emu* e) {
    if (e->fatal_error) return;

    if (e->tia.clear_next) {
        memset(e->tia.fb, 0, sizeof(e->tia.fb));
        e->tia.clear_next = false;
    }

    e->tia.frame_pulse = false;

    for (;;) {
        if (e->reset_request) system_reset(e);

        int cyc = cpu6507_step(&e->cpu, e, (cpu_read_fn)bus_cpu_read, (cpu_write_fn)bus_cpu_write);
        if (cyc < 1) cyc = 1;
        step_cpu_cycles(e, cyc);

        if (e->cpu.jammed) {
            // cpu6507_step already printed the error (SDL_Log).
            // Mark fatal so the app can bail immediately.
            e->fatal_error = true;
            return;
        }

        if (e->tia.frame_pulse) {
            e->tia.frame_pulse = false;
            return;
        }
    }
}

const u32* emu_framebuffer(const Emu* e) {
    return tia_fb_const(&e->tia);
}

bool emu_has_fatal_error(const Emu* e) {
    return e->fatal_error;
}
