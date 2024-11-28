#pragma once
#include <stdio.h>
#include <stdint.h>

#if !defined(NDEBUG)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...)
#endif

#define NES_WIDTH 256
#define NES_HEIGHT 240

#define VISIBLE_SCANLINES NES_HEIGHT
#define NTSC_SCANLINES_PER_FRAME 261

#define CPU_CYCLES_PER_SCANLINE 114 // ppu / 3

enum {
    BIT_7 = 1 << 7,
    BIT_6 = 1 << 6,
    BIT_5 = 1 << 5,
    BIT_4 = 1 << 4,
    BIT_3 = 1 << 3,
    BIT_2 = 1 << 2,
    BIT_1 = 1 << 1,
    BIT_0 = 1
};

extern uint8_t RAM[2048];
extern uint8_t ROM[1024 << 10];


extern uint8_t VRAM[2048];
extern uint8_t OAM[256];
extern uint8_t PALETTE[64];
