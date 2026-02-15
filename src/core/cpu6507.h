#pragma once
#include "util/common.h"

typedef struct CPU6507 {
    u8  A, X, Y, P, S;
    u16 PC;
    bool jammed;
    bool stalled;
} CPU6507;

enum {
    CPU_FLAG_C = 0x01,
    CPU_FLAG_Z = 0x02,
    CPU_FLAG_I = 0x04,
    CPU_FLAG_D = 0x08,
    CPU_FLAG_B = 0x10,
    CPU_FLAG_U = 0x20,
    CPU_FLAG_V = 0x40,
    CPU_FLAG_N = 0x80
};

typedef u8  (*cpu_read_fn)(void* ctx, u16 addr);
typedef void (*cpu_write_fn)(void* ctx, u16 addr, u8 v);

void cpu6507_reset(CPU6507* c, void* ctx, cpu_read_fn rfn, cpu_write_fn wfn);
int  cpu6507_step(CPU6507* c, void* ctx, cpu_read_fn rfn, cpu_write_fn wfn);
