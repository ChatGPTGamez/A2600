#include "app/video.h"
#include "core/tia.h"
#include <string.h>

bool video_init(Video* v, const char* title, int scale) {
    memset(v, 0, sizeof(*v));

    int w = FB_W * scale;
    int h = FB_H * scale;

    v->win = SDL_CreateWindow(title, w, h, 0);
    if (!v->win) return false;

    v->ren = SDL_CreateRenderer(v->win, NULL);
    if (!v->ren) return false;

    v->tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!v->tex) return false;

    SDL_SetTextureScaleMode(v->tex, SDL_SCALEMODE_NEAREST);
    return true;
}

void video_shutdown(Video* v) {
    if (v->tex) SDL_DestroyTexture(v->tex);
    if (v->ren) SDL_DestroyRenderer(v->ren);
    if (v->win) SDL_DestroyWindow(v->win);
    memset(v, 0, sizeof(*v));
}

void video_present(Video* v, const u32* fb) {
    void* pixels = NULL;
    int pitch = 0;

    if (!SDL_LockTexture(v->tex, NULL, &pixels, &pitch)) {
        return;
    }

    for (int y = 0; y < FB_H; y++) {
        memcpy((u8*)pixels + y * pitch, fb + y * FB_W, (size_t)FB_W * 4);
    }

    SDL_UnlockTexture(v->tex);

    SDL_RenderClear(v->ren);

    int ww = 0, wh = 0;
    SDL_GetWindowSize(v->win, &ww, &wh);
    SDL_FRect dst = { 0.0f, 0.0f, (float)ww, (float)wh };
    SDL_RenderTexture(v->ren, v->tex, NULL, &dst);

    SDL_RenderPresent(v->ren);
}
