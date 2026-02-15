#pragma once
#include "util/common.h"
#include <SDL3/SDL.h>

typedef struct Video {
    SDL_Window* win;
    SDL_Renderer* ren;
    SDL_Texture* tex;
} Video;

bool video_init(Video* v, const char* title, int scale);
void video_shutdown(Video* v);
void video_present(Video* v, const u32* fb);
