#include <SDL3/SDL_main.h>
#include <stdio.h>
#include "app/app.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s rom.bin\n", argv[0]);
        return 2;
    }

    return app_run(argv[1]) ? 0 : 1;
}
