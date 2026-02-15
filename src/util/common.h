#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  s32;

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif
