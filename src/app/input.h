#pragma once
#include "util/common.h"
#include <SDL3/SDL.h>

typedef struct InputState {
    bool up, down, left, right, fire;
} InputState;

void input_handle_event(InputState* in, const SDL_Event* e, bool* out_running, bool* out_reset);
