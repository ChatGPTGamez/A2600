#include "app/app.h"
#include "app/video.h"
#include "app/input.h"
#include "emu/emu.h"
#include "util/log.h"
#include <SDL3/SDL.h>

bool app_run(const char* rom_path) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        log_fatal("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    Video video;
    if (!video_init(&video, "A2600 (modular SDL3)", 4)) {
        log_fatal("Video init failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    Emu emu;
    emu_init(&emu);

    if (!rom_path || !rom_path[0]) {
        log_fatal("No ROM path provided.");
        video_shutdown(&video);
        SDL_Quit();
        return false;
    }

    if (!emu_load_rom_path(&emu, rom_path)) {
        log_fatal("Failed to load ROM (supported sizes: 2K/4K/8K-F8).");
        video_shutdown(&video);
        SDL_Quit();
        return false;
    }

    bool running = true;
    bool reset_pulse = false;
    InputState in = (InputState){0};

    u64 last_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_handle_event(&in, &e, &running, &reset_pulse);
        }

        emu_set_joy(&emu, in.up, in.down, in.left, in.right, in.fire);
        if (reset_pulse) { emu_request_reset(&emu); reset_pulse = false; }

        emu_run_until_frame(&emu);

        // HARD EXIT: unknown opcode / JAM
        if (emu_has_fatal_error(&emu)) {
            running = false;
            break;
        }

        video_present(&video, emu_framebuffer(&emu));

        u64 now = SDL_GetTicks();
        u64 frame_ms = now - last_ticks;
        last_ticks = now;
        if (frame_ms < 16) SDL_Delay((u32)(16 - frame_ms));
    }

    emu_unload_rom(&emu);
    video_shutdown(&video);
    SDL_Quit();
    return true;
}
