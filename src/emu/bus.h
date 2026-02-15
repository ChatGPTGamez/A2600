#pragma once
#include "util/common.h"

typedef struct Emu Emu;

u8  bus_cpu_read(Emu* e, u16 addr);
void bus_cpu_write(Emu* e, u16 addr, u8 v);
