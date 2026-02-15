#include "app/input.h"

void input_handle_event(InputState* in, const SDL_Event* e, bool* out_running, bool* out_reset) {
    if (e->type == SDL_EVENT_QUIT) { *out_running = false; return; }

    if (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) {
        bool down = (e->type == SDL_EVENT_KEY_DOWN);
        SDL_Keycode key = e->key.key;

        if (down && key == SDLK_ESCAPE) { *out_running = false; return; }
        if (down && key == SDLK_R)      { *out_reset = true; return; }

        if (key == SDLK_UP)    in->up    = down;
        if (key == SDLK_DOWN)  in->down  = down;
        if (key == SDLK_LEFT)  in->left  = down;
        if (key == SDLK_RIGHT) in->right = down;
        if (key == SDLK_SPACE) in->fire  = down;
    }
}
