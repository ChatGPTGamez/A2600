#include "core/cart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_kil_opcode(u8 op) {
    switch (op) {
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:
            return true;
        default:
            return false;
    }
}

static u16 f8_reset_vector(const Cart* c, int bank) {
    size_t base = (size_t)bank * 4096;
    u8 lo = c->data[base + 0x0FFC];
    u8 hi = c->data[base + 0x0FFD];
    return (u16)lo | ((u16)hi << 8);
}

static u8 f8_peek_opcode_at(const Cart* c, int bank, u16 pc) {
    u16 a13 = pc & 0x1FFF;
    if ((a13 & 0x1000) == 0) return 0xFF;
    u16 off = (u16)(a13 - 0x1000);
    return c->data[(size_t)bank * 4096 + (off & 0x0FFF)];
}

bool cart_load_path(Cart* c, const char* path) {
    memset(c, 0, sizeof(*c));

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }

    c->data = (u8*)malloc((size_t)sz);
    if (!c->data) { fclose(f); return false; }

    if (fread(c->data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(c->data);
        memset(c, 0, sizeof(*c));
        return false;
    }
    fclose(f);

    c->size = (size_t)sz;

    if (c->size == 2048) c->is_2k = true;
    else if (c->size == 4096) c->is_4k = true;
    else if (c->size == 8192) c->is_f8 = true;
    else { cart_free(c); return false; }

    c->bank = 1;

    if (c->is_f8) {
        u16 rv0 = f8_reset_vector(c, 0);
        u16 rv1 = f8_reset_vector(c, 1);

        u8 op0 = f8_peek_opcode_at(c, 0, rv0);
        u8 op1 = f8_peek_opcode_at(c, 1, rv1);

        bool rv0_ok = ((rv0 & 0xF000) == 0xF000) && !is_kil_opcode(op0);
        bool rv1_ok = ((rv1 & 0xF000) == 0xF000) && !is_kil_opcode(op1);

        if (rv0_ok && !rv1_ok) c->bank = 0;
        else if (rv1_ok && !rv0_ok) c->bank = 1;
        else c->bank = 1;
    }

    return true;
}

void cart_free(Cart* c) {
    if (c->data) free(c->data);
    memset(c, 0, sizeof(*c));
}

u8 cart_cpu_read(Cart* c, u16 addr) {
    u16 a13 = addr & 0x1FFF;
    if ((a13 & 0x1000) == 0) return 0xFF;

    u16 off = (u16)(a13 - 0x1000);

    if (c->is_2k) {
        return c->data[off & 0x07FF];
    } else if (c->is_4k) {
        return c->data[off & 0x0FFF];
    } else if (c->is_f8) {
        if (a13 == 0x1FF8) c->bank = 0;
        else if (a13 == 0x1FF9) c->bank = 1;

        size_t base = (size_t)c->bank * 4096;
        return c->data[base + (off & 0x0FFF)];
    }

    return 0xFF;
}
