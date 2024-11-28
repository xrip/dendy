#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")

#include <stdio.h>
#include <windows.h>

#include "nes.h"
#include "ppu.h"
#include "m6502/m6502.h"
#include "win32/MiniFB.h"
#include "win32/audio.h"

uint8_t RAM[2048] = {0};
uint8_t ROM[1024 << 10] = {0};
M6502 cpu;


uint8_t SCREEN[NES_WIDTH * NES_HEIGHT + 8] = {0}; // +8 possible sprite overflow

typedef struct __attribute__((packed)) {
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





static uint8_t *key_status;

void HandleInput(WPARAM wParam, BOOL isKeyDown) {
}
uint16_t prg_rom_mosk;
void parse_ines_header(ines_header_t *INES) {
    if (memcmp(INES->magic, "NES\x1A", 4) != 0) {
        fprintf(stderr, "Invalid iNES file %s!\n", INES->magic);
        return;
    }
    prg_rom_mosk = (INES->prg_rom_size * 16 << 10);
    ppu.chr_rom = &ROM[INES->prg_rom_size * 16 << 10];
    ppu.mirroring = INES->flags6 & 0x01;
    printf("iNES Header Info:\n");
    printf("PRG ROM Size: %d KB\n", INES->prg_rom_size * 16);
    printf("CHR ROM Size: %d KB\n", INES->chr_rom_size * 8);
    printf("Mapper: %d\n", ((INES->flags7 & 0xF0) | (INES->flags6 >> 4)));
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
    return rom_size;
}


uint8_t buttons = 0;

uint8_t Rd6502(uint16_t address) {
    // printf("R6502: %02x\n", address);
    if (address < 0x2000) {
        return RAM[address & 2047];
    }
    // PPU
    if (address < 0x4000) {
        return ppu_read(address);
    }

    // APU
    if (address < 0x4017) {
        switch (address) {
            case 0x4016: // Gamepad #1
                const uint8_t bit = buttons & 1;
                buttons >>= 1;
                return bit;
        }
    }

    if (address < 0x6000) {
        //Cartridge Expansion Area almost 8K
        return 0xFF;
    }

    if (address < 0x8000) {
        // Cartridge SRAM Area 8K
        return 0xFF;
    }

    return ROM[(address - 0x8000) % prg_rom_mosk]; // do a trick with -0x8000 pointer
}

void Wr6502(uint16_t address, uint8_t value) {
    // printf("Wr6502: %04x %02x\n", address, value);
    if (address < 0x2000) {
        RAM[address & 2047] = value;
        return;
    }

    if (address < 0x4000) {
        return ppu_write(address, value);
    }

    if (address == 0x4014) {
        const uint16_t source_address = value << 8; // page
        for (uint8_t i = 0; i < 255; i++) {
            OAM[i] = RAM[source_address + i];
        }
        // todo: add cpu cycles
        return;
    }

    if (address == 0x4016 && value) {
        buttons = 0x0;

        if (key_status['Z']) buttons |= BIT_0;
        if (key_status['X']) buttons |= BIT_1;

        if (key_status[VK_SPACE]) buttons |= BIT_2;

        if (key_status[VK_RETURN]) buttons |= BIT_3;


        if (key_status[VK_UP]) buttons |= BIT_4;
        if (key_status[VK_DOWN]) buttons |= BIT_5;

        if (key_status[VK_LEFT]) buttons |= BIT_6; //
        if (key_status[VK_RIGHT]) buttons |= BIT_7;

        return;
    }
}

byte Patch6502(register byte Op, register M6502 *R) {
}

/** /
static inline void dendy_frame() {
    uint16_t scanline = 0;
    uint8_t * screen = SCREEN;

    const uint8_t * bg_tiles = &ROM[prg_rom_size + ppu_bg_pattern_address];
    const uint8_t * sp_tiles = &ROM[prg_rom_size + ppu_sprite_pattern_address];
    const uint8_t * nametable = &VRAM[ppu_base_nametable_address];
    const uint8_t * attribute_table = nametable + 0x3C0;

    for (; scanline < VISIBLE_SCANLINES; scanline++) {
        Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);

        const uint8_t row = scanline / TILE_HEIGHT;
        const uint8_t * tile_index = &nametable[row * 32];
        const uint8_t attribute_index = row / 4 * 8;

        // background
        for (uint8_t column = 0; column < 32; ++column) {
            const uint16_t tile_address = (scanline % 8 + 16 * *tile_index++) ;
            const uint8_t attribute_byte = attribute_table[attribute_index + column / 4];

            uint8_t block_x = (column / 2) % 2; // 0 = left, 1 = right
            uint8_t block_y = (row / 2) % 2; // 0 = top, 1 = bottom
            uint8_t bit_shift = (block_y * 2 + block_x) * 2; // 2 bits per block
            uint8_t palette_index = (attribute_byte >> bit_shift) & 0x03;

            const uint8_t tile_low = bg_tiles[tile_address];
            const uint8_t tile_high = bg_tiles[tile_address + 8];

            for (uint8_t tile_x = 0; tile_x < TILE_WIDTH; ++tile_x) {
                const uint8_t bit = 7 - tile_x;
                *screen++ = (palette_index << 2) | (tile_high >> bit & 1) << 1 | (tile_low >> bit) & 1;

            }

        }

        for (uint8_t sprite_index = 0; sprite_index < 64; sprite_index++) {
            uint8_t y = OAM[4 * sprite_index + 0] + ppu_scroll_y;       // Y-coordinate
            if (scanline < y || scanline > y + 8 || y >= 240) continue;

            uint8_t tile_index = OAM[4 * sprite_index + 1]; // Tile index
            uint8_t attributes = OAM[4 * sprite_index + 2]; // Attributes
            uint8_t x = OAM[4 * sprite_index + 3] + ppu_scroll_x;       // X-coordinate

            // Determine the sprite palette and flipping
            uint8_t palette_index = (attributes & 0x03);       // Bits 0-1
            uint8_t priority = (attributes & 0x20) != 0;          // Bit 5
            uint8_t flip_horizontally = (attributes & 0x40) != 0; // Bit 6
            uint8_t flip_vertically = (attributes & 0x80) != 0;   // Bit 7

            // Adjust tile index if using 8x16 sprites
            if (ppu_sprite_size == 16) {
                tile_index &= 0xFE; // Use even tile index (0, 2, 4, ...)
            }

            // Loop through each pixel in the sprite
            // for (uint8_t py = 0; py < 8; py++)
            uint8_t py = scanline % 8;
                {
                uint8_t tile_row = flip_vertically ? 7 - py : py;

                uint8_t bitplane_0 = sp_tiles[tile_index * 16 + tile_row];
                uint8_t bitplane_1 = sp_tiles[tile_index * 16 + tile_row + 8];

                for (uint8_t px = 0; px < 8; px++) {
                    uint8_t tile_col = flip_horizontally ? px : 7 - px;

                    // Get the 2-bit pixel value from the pattern table
                    uint8_t low_bit = (bitplane_0 >> tile_col) & 0x01;
                    uint8_t high_bit = (bitplane_1 >> tile_col) & 0x01;
                    uint8_t pixel_color = (high_bit << 1) | low_bit;

                    // Skip transparent pixels
                    if (pixel_color == 0) continue;

                    // Calculate screen position
                    uint8_t screen_x = x + px - ppu_scroll_x;
                    uint8_t screen_y = y + py - ppu_scroll_y;

                    // Skip if off-screen
                    if (screen_x >= 256 || screen_y >= 240) continue;

                    // Combine palette index and pixel color
                    uint8_t final_color = (palette_index << 2) | pixel_color;

                    // Blend sprite with background based on priority
                    if (!priority || SCREEN[screen_y * 256 + screen_x] == 0) {
                        SCREEN[screen_y * 256 + screen_x] = SCREEN[final_color];
                    }
                }
            }
        }

    }
    ppu_status_register |= BIT_7;
    Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE * 2); // 240-242

    scanline += 2;



    for (; scanline < NTSC_SCANLINES_PER_FRAME; scanline++) {
        // 243-262
        if (ppu_nmi_enabled) {
            Int6502(&cpu, INT_NMI);
            // printf("NMI\n");
        }

        Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);
    }
}
/**/

static void dendy_frame() {
    uint8_t *screen = SCREEN;
    uint16_t scanline = 0;
    uint8_t sprite_height = ppu.sprite_height;

    ppu.status &= ~BIT_7;

    for (scanline = 0; scanline < VISIBLE_SCANLINES; ++scanline) {
        const uint8_t tile_row = scanline / TILE_HEIGHT;
        const uint8_t fine_y = scanline & 7;
        if (ppu.background_enabled) {
            const uint8_t *tile_index = &ppu.nametable[tile_row * 32];
            const uint8_t *attribute_table = &ppu.nametable[0x03C0 + tile_row / 4 * 8];

            for (uint8_t tile_col = 0; tile_col < 32; ++tile_col) {
                const uint8_t tile_id = *tile_index++;

                const uint16_t tile_address = fine_y + 16 * tile_id;
                const uint8_t tile_low = ppu.background[tile_address];
                const uint8_t tile_high = ppu.background[tile_address + 8];

                // Precompute attribute table access
                const uint8_t attr_byte = attribute_table[tile_col / 4];

                // Precompute quadrant shift
                const uint8_t quadrant = tile_row % 4 / 2 * 2 + tile_col % 4 / 2;
                const uint8_t palette_index = attr_byte >> quadrant * 2 & 0x03;

                // Unroll inner loop for TILE_WIDTH (8 pixels)
                for (uint8_t bit = 7, tile_x = 0; tile_x < TILE_WIDTH; ++tile_x, --bit) {
                    *screen++ = palette_index << 2 | (tile_high >> bit & 1) << 1 | tile_low >> bit & 1;
                }
            }
        }
        if (ppu.sprites_enabled) {
            for (uint16_t sprite_index = 0; sprite_index != 256; sprite_index+=4) {
                const uint8_t sprite_y = OAM[sprite_index] + 1; // Y-coordinate

                if (scanline < sprite_y || scanline > sprite_y + 8 || sprite_y >= 240) continue;

                uint8_t tile_index = OAM[sprite_index + 1]; // Tile index
                const uint8_t attributes = OAM[sprite_index + 2]; // Attributes
                unsigned char sprite_x = OAM[sprite_index + 3]; // X-coordinate

                // Determine the sprite palette and flipping
                const uint8_t palette_index = attributes & 0x03; // Bits 0-1
                const uint8_t priority = (attributes & 0x20) != 0; // Bit 5
                const uint8_t flip_horizontally = (attributes & 0x40) != 0; // Bit 6
                const uint8_t flip_vertically = (attributes & 0x80) != 0; // Bit 7

                // Adjust tile index if using 8x16 sprites
                if (sprite_height == 16) {
                    tile_index &= 0xFE; // Use even tile index (0, 2, 4, ...)
                }
                const uint8_t row = flip_vertically ? 7 - fine_y : fine_y;

                const uint16_t tile_address = tile_index * 16 + row;
                const uint8_t tile_low = ppu.sprites[tile_address];
                const uint8_t tile_high = ppu.sprites[tile_address + 8];

                for (uint8_t px = 0; px < 8; px++) {
                    const uint8_t tile_col = flip_horizontally ? px : 7 - px;

                    // Get the 2-bit pixel value from the pattern table
                    const uint8_t low_bit = tile_low >> tile_col & 1;
                    const uint8_t high_bit = tile_high >> tile_col & 1;
                    const uint8_t pixel_color = high_bit << 1 | low_bit;

                    // Skip transparent pixels
                    if (pixel_color == 0) continue;

                    // Calculate screen position
                    const uint8_t screen_x = sprite_x + px;
                    const uint8_t screen_y = sprite_y + fine_y;

                    // Skip if off-screen
                    if (screen_x >= 256 || screen_y >= 240) continue;

                    // Combine palette index and pixel color
                    const uint8_t final_color = palette_index << 2 | pixel_color;

                    // Blend sprite with background based on priority
                    if (!priority || SCREEN[screen_y * NES_WIDTH + screen_x] == 0) {
                        SCREEN[screen_y * NES_WIDTH + screen_x] = final_color;
                    }
                }
            }
        }

        Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);
    }

    Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);
    scanline++;

    if (mfb_update(SCREEN, 60) == -1)
        exit(1);

    ppu.status |= 0x80; // Set VBLANK

    for (; scanline < NTSC_SCANLINES_PER_FRAME; ++scanline) {
        Exec6502(&cpu, CPU_CYCLES_PER_SCANLINE);

        if (ppu.nmi_enabled) {
            Int6502(&cpu, INT_NMI);
            // printf("NMI occurred\n");
        }
    }
}

int main(const int argc, char **argv) {
    int scale = 4;
    if (!argv[1]) {
        printf("Usage: dendy.exe <rom.bin> [scale_factor]\n");
        exit(-1);
    }
    if (argc > 2) {
        scale = atoi(argv[2]);
    }

    const char *filename = argv[1];
    // const size_t len = strlen(filename);
    readfile(filename, ROM);

    if (!mfb_open("Dendy", NES_WIDTH, NES_HEIGHT, scale))
        return 1;

    // for (uint8_t i = 0; i < 255; ++i) {
        // mfb_set_pallete(i, nes_palette_raw[i & 63]);
    // }
mfb_set_pallete_array(nes_palette_raw, 0, 255);
    key_status = (uint8_t *) mfb_keystatus();

    // CreateThread(NULL, 0, SoundThread, NULL, 0, NULL);
    // CreateThread(NULL, 0, TicksThread, NULL, 0, NULL);

    Reset6502(&cpu);

    memset(RAM, 0, sizeof(RAM));
    memset(VRAM, 0, sizeof(VRAM));
    memset(SCREEN, 0, NES_WIDTH * NES_HEIGHT);


    while (1) {
        dendy_frame();
    }
}
