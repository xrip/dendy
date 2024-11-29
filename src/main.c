// https://emudev.de/nes-emulator/palettes-attribute-tables-and-sprites/
// https://austinmorlan.com/posts/nes_rendering_overview/
// https://www.copetti.org/writings/consoles/nes/#graphics
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")

#include <stdio.h>
#include <windows.h>

#include "nes.h"
#include "ppu.h"
#include "m6502/m6502.h"
#include "win32/MiniFB.h"

uint8_t RAM[2048] = {0};
uint8_t ROM[1024 << 10] = {0};
M6502 cpu;
uint8_t SCREEN[NES_WIDTH * NES_HEIGHT + 8] = {0}; // +8 possible sprite overflow

static uint8_t *key_status;
static uint8_t buttons = 0;
static uint16_t prg_rom_mask;
static uint8_t mapper = 0;
static uint8_t banks_count  = 0;

typedef struct {
    char magic[4]; // iNES magic string "NES\x1A"
    uint8_t prg_rom_size; // PRG ROM size in 16 KB units
    uint8_t chr_rom_size; // CHR ROM size in 8 KB units
    uint8_t flags6; // Flags 6: Mapper, mirroring, battery, etc.
    uint8_t flags7; // Flags 7: Mapper, VS/PlayChoice, NES 2.0 indicator
    uint8_t prg_ram_size; // PRG RAM size in 8 KB units (0 = default 8 KB)
    uint8_t flags9; // Flags 9: TV system (NTSC/PAL)
    uint8_t flags10; // Flags 10: Miscellaneous
    uint8_t padding[5]; // Padding (should be zero)
} ines_header_t;

uint8_t Patch6502(register uint8_t Op, register M6502 *R) {
}

void HandleInput(WPARAM wParam, BOOL isKeyDown) {
}

void parse_ines_header(ines_header_t *INES) {
    if (memcmp(INES->magic, "NES\x1A", 4) != 0) {
        fprintf(stderr, "Invalid iNES file %s!\n", INES->magic);
        return;
    }
    prg_rom_mask = (INES->prg_rom_size * 16 << 10);
    ppu.chr_rom = &ROM[INES->prg_rom_size * 16 << 10];
    ppu.mirroring = INES->flags6 & 0x01;
    mapper = INES->flags7 & 0xF0 | INES->flags6 >> 4;
    printf("iNES Header Info:\n");
    printf("PRG ROM Size: %d KB\n", INES->prg_rom_size * 16);
    printf("CHR ROM Size: %d KB\n", INES->chr_rom_size * 8);
    printf("Mapper: %d\n", mapper);
    printf("Mirroring: %s\n", (INES->flags6 & 0x01) ? "Vertical" : "Horizontal");
    printf("Battery-backed Save: %s\n", (INES->flags6 & 0x02) ? "Yes" : "No");
    printf("Trainer Present: %s\n", (INES->flags6 & 0x04) ? "Yes" : "No");
    printf("Four-screen Mode: %s\n", (INES->flags6 & 0x08) ? "Yes" : "No");
    printf("TV System: %s\n", (INES->flags9 & 0x01) ? "PAL" : "NTSC");
    printf("PRG RAM Size: %d KB\n", INES->prg_ram_size ? INES->prg_ram_size * 8 : 8);

    printf("\n\n\n");
}

static inline size_t readfile(const char *pathname, uint8_t *dst) {
    FILE *file = fopen(pathname, "rb");
    fseek(file, 0, SEEK_END);
    const size_t rom_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    ines_header_t INES = {0};
    fread(&INES, sizeof(ines_header_t), 1, file);
    parse_ines_header(&INES);

    fread(dst, sizeof(uint8_t), rom_size, file);
    fclose(file);
    banks_count = rom_size / 0x2000;
    return rom_size;
}

// Memory read handler for 6502 CPU
uint8_t Rd6502(uint16_t address) {
    if (address < 0x2000) {
        return RAM[address & 2047];
    }

    if (address < 0x4000) {
        return ppu_read(address);
    }

    if (address == 0x4016) {
        const uint8_t bit = buttons & 1;
        buttons >>= 1;
        return bit;
    }

    if (address >= 0x8000) {
        return ROM[(address - 0x8000) % prg_rom_mask];
    }

    return 0xFF;
}

// Memory write handler for 6502 CPU
void Wr6502(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        RAM[address & 2047] = value;
    } else if (address < 0x4000) {
        ppu_write(address, value);
    } else if (address == 0x4014) {
        memcpy(OAM, &RAM[value << 8], 256);
    } else if (address == 0x4016 && value) {
        buttons = 0;
        if (key_status['Z']) buttons |= BIT_0;
        if (key_status['X']) buttons |= BIT_1;
        if (key_status[VK_SPACE]) buttons |= BIT_2;
        if (key_status[VK_RETURN]) buttons |= BIT_3;
        if (key_status[VK_UP]) buttons |= BIT_4;
        if (key_status[VK_DOWN]) buttons |= BIT_5;
        if (key_status[VK_LEFT]) buttons |= BIT_6;
        if (key_status[VK_RIGHT]) buttons |= BIT_7;
    }
    if (address >= 0x8000) {
        switch (mapper) {
            case 3:
                printf("bank switch %x %i\n",address, value % banks_count) ;
                ppu.chr_rom = &ROM[prg_rom_mask + (value % banks_count) * 0x2000];
            break;
        }
    }
}


static void dendy_frame() {
    uint8_t *screen = SCREEN;
    uint16_t scanline = 0;
    const uint8_t sprite_height = ppu.sprite_height;
    const uint8_t sprite_index_mask = sprite_height == 16 ? 0xFE : 0xFF;

    ppu.status &= ~BIT_7;

    for (scanline = 0; scanline < VISIBLE_SCANLINES; ++scanline) {
        const uint16_t y = scanline + ppu.scroll_y;
        const uint8_t fine_y = y & 7;

        if (ppu.background_enabled) {
            const uint8_t row = y / TILE_HEIGHT % 30;
            const uint8_t tile_offset_x = ppu.scroll_x / TILE_WIDTH; // Coarse scroll X

            const uint8_t *tiles = &ppu.nametable[row * 32];
            const uint8_t *attribute_table = &ppu.nametable[0x03C0 + row / 4 * 8];

            for (uint8_t tile_column = 0; tile_column < 32; ++tile_column) {
                const uint8_t column = (tile_column + tile_offset_x) % 32;
                const uint16_t tile_address = fine_y + 16 * tiles[column];

                const uint8_t tile_low_byte = ppu.background[tile_address];
                const uint8_t tile_high_byte = ppu.background[tile_address + 8];

                // Precompute attribute table access
                const uint8_t attr_byte = attribute_table[column / 4];

                // Precompute quadrant shift
                const uint8_t quadrant = row % 4 / 2 * 2 + column % 4 / 2;
                const uint8_t palette_index = attr_byte >> quadrant * 2 & 0x03;

                // Unroll inner loop for TILE_WIDTH (8 pixels)
                for (uint8_t bit = 7; bit < TILE_WIDTH; --bit) {
                    *screen++ = palette_index << 2 | (tile_high_byte >> bit & 1) << 1 | tile_low_byte >> bit & 1;
                }


            }
        }
        if (ppu.sprites_enabled) {
            for (uint16_t sprite = 0; sprite != 256; sprite+=4) {
                const uint8_t sprite_y = OAM[sprite] + 1; // Y-coordinate
                if (scanline < sprite_y || scanline >= sprite_y + sprite_height || sprite_y >= 240) continue;

                const uint8_t sprite_index = OAM[sprite + 1] & sprite_index_mask; // Tile index
                const uint8_t attributes = OAM[sprite + 2]; // Attributes
                const uint8_t sprite_x = OAM[sprite + 3]; // X-coordinate

                // Determine the sprite palette and flipping
                const uint8_t palette_index = attributes & 3; // Bits 0-1
                const uint8_t priority = attributes & BIT_5;
                const uint8_t flip_horizontally = attributes & BIT_6;
                const uint8_t flip_vertically = attributes & BIT_7;

                const uint8_t row = flip_vertically ? sprite_height - 1 - fine_y : fine_y;

                const uint16_t sprite_address = sprite_index * 16 + row;
                const uint8_t sprite_low_byte = ppu.sprites[sprite_address];
                const uint8_t sprite_high_byte = ppu.sprites[sprite_address + 8];

                uint8_t mask = flip_horizontally ? 0x01 : 0x80;
                const uint16_t screen_row = (sprite_y + fine_y) * NES_WIDTH + sprite_x;

                for (uint8_t px = 0; px < 8; ++px, mask = flip_horizontally ? mask << 1 : mask >> 1) {
                    const uint8_t pixel_color = (sprite_high_byte & mask ? 2 : 0) | (sprite_low_byte & mask ? 1 : 0);
                    if (pixel_color != 0 && !priority) {
                        SCREEN[screen_row + px] = palette_index << 2 | pixel_color;
                    }
                }
            }
        }

        Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);
    }

    Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);
    scanline++;

    if (mfb_update(SCREEN, 60) == -1)
        exit(EXIT_SUCCESS);

    ppu.status |= BIT_7; // Set VBLANK

    for (; scanline < NTSC_SCANLINES_PER_FRAME; ++scanline) {
        Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);

        if (ppu.nmi_enabled) {
            Int6502(&cpu, INT_NMI);
            // printf("NMI occurred\n");
        }
    }
}

int main(const int argc, char **argv) {
    const int scale = argc > 2 ? atoi(argv[2]) : 4;

    if (!argv[1]) {
        printf("Usage: dendy.exe <rom.bin> [scale_factor]\n");
        return EXIT_FAILURE;
    }


    readfile(argv[1], ROM);

    if (!mfb_open("Dendy", NES_WIDTH, NES_HEIGHT, scale))
        return EXIT_FAILURE;

    mfb_set_pallete_array(nes_palette_raw, 0, 255);
    key_status = (uint8_t *) mfb_keystatus();

    Reset6502(&cpu);

    memset(RAM, 0, sizeof(RAM));
    memset(VRAM, 0, sizeof(VRAM));
    memset(SCREEN, 0, NES_WIDTH * NES_HEIGHT);


    while (1) {
        dendy_frame();
    }

    return EXIT_SUCCESS;
}
