#pragma once
#include "util/common.h"

typedef struct Cart {
    u8* data;
    size_t size;
    bool is_2k;
    bool is_4k;
    bool is_f8;
    int bank;
} Cart;

bool cart_load_path(Cart* c, const char* path);
void cart_free(Cart* c);

u8 cart_cpu_read(Cart* c, u16 addr); // includes bankswitch side-effects (F8)
