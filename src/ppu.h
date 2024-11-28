#pragma once
#include "nes.h"

#define TILE_WIDTH 8
#define TILE_HEIGHT 8

typedef struct {
    uint8_t status;
    uint16_t address;

    uint8_t nmi_enabled;
    uint8_t * chr_rom;
    uint8_t * nametable;
    uint8_t * sprites;
    uint8_t * background;

    uint8_t sprite_height;
    uint8_t address_step;

    // |||| ||+-- 1: Show background in leftmost 8 pixels of screen, 0: Hide
    // |||| |+--- 1: Show sprites in leftmost 8 pixels of screen, 0: Hide
    uint8_t background_enabled;
    uint8_t sprites_enabled;

    uint16_t scroll_x;
    uint16_t scroll_y;

    uint8_t mirroring; // 1 - vertical ; 0 - horizontal

} PPU;

extern PPU ppu;

uint8_t ppu_read(uint16_t address);

void ppu_write(uint16_t address, uint8_t data);
