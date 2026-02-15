#pragma once
#include "util/common.h"
#include "core/cpu6507.h"
#include "core/tia.h"
#include "core/riot.h"
#include "core/cart.h"

typedef struct Emu {
    CPU6507 cpu;
    TIA tia;
    RIOT riot;
    Cart cart;

    bool has_cart;

    // input state
    bool joy_up, joy_down, joy_left, joy_right, joy_fire;

    // control
    bool reset_request;

    // fatal (unknown opcode / JAM)
    bool fatal_error;
} Emu;

void emu_init(Emu* e);
bool emu_load_rom_path(Emu* e, const char* path);
void emu_unload_rom(Emu* e);

void emu_request_reset(Emu* e);

void emu_set_joy(Emu* e, bool up, bool down, bool left, bool right, bool fire);

// Run until VSYNC falling-edge triggers frame_pulse (same as your run_until_frame)
void emu_run_until_frame(Emu* e);

const u32* emu_framebuffer(const Emu* e);

bool emu_has_fatal_error(const Emu* e);
