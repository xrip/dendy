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


extern uint8_t VRAM[16384];
extern uint8_t OAM[256];
extern uint8_t PALETTE[64];
// RGB888 palette
static const int nes_palette_raw[64] = {
    0x6D6D6D, 0x002492, 0x0000DB, 0x6D49DB,
                            0x92006D, 0xB6006D, 0xB62400, 0x924900,
                            0x6D4900, 0x244900, 0x006D24, 0x009200,
                            0x004949, 0x000000, 0x000000, 0x000000,
                            0xB6B6B6, 0x006DDB, 0x0049FF, 0x9200FF,
                            0xB600FF, 0xFF0092, 0xFF0000, 0xDB6D00,
                            0x926D00, 0x249200, 0x009200, 0x00B66D,
                            0x009292, 0x242424, 0x000000, 0x000000,
                            0xFFFFFF, 0x6DB6FF, 0x9292FF, 0xDB6DFF,
                            0xFF00FF, 0xFF6DFF, 0xFF9200, 0xFFB600,
                            0xDBDB00, 0x6DDB00, 0x00FF00, 0x49FFDB,
                            0x00FFFF, 0x494949, 0x000000, 0x000000,
                            0xFFFFFF, 0xB6DBFF, 0xDBB6FF, 0xFFB6FF,
                            0xFF92FF, 0xFFB6B6, 0xFFDB92, 0xFFFF49,
                            0xFFFF6D, 0xB6FF49, 0x92FF6D, 0x49FFDB,
                            0x92DBFF, 0x929292, 0x000000, 0x000000

};