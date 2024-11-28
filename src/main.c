#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")

#include <stdio.h>
#include <windows.h>

#include "m6502/m6502.h"
#include "win32/MiniFB.h"
#include "win32/audio.h"

#define NES_WIDTH 256
#define NES_HEIGHT 240

#define VISIBLE_SCANLINES 240
#define VISIBLE_DOTS 256
#define NTSC_SCANLINES_PER_FRAME 261
#define PAL_SCANLINES_PER_FRAME 311
#define DOTS_PER_SCANLINE 341
#define END_DOT 340

#define INES_HEADER_SIZE 16


// https://www.nesdev.org/wiki/Cycle_reference_chart
#define CPU_SPEED 17897725 // The NTSC NES runs at 1.7897725MHz
#define CPU_CYCLES_PER_SCANLINE 114 // ppu / 3
// scanline 341 pixels
// vblank  cycles 20*114=2273
// hblank  28
// frame 29780.5 -- odd even to round
uint8_t SCREEN[NES_WIDTH * NES_HEIGHT + 8] = {0}; // +8 possible sprite overflow

#define RAM_SIZE 2048
uint8_t RAM[RAM_SIZE] = {0};
uint8_t OAM[256] = {0};
uint8_t VRAM[2048] = {0};
uint8_t PALETTE_RAM[64] = {0};

uint8_t CARTRIDGE[1024 << 10] = {0}; // todo variable name
uint8_t *ROM = &CARTRIDGE[INES_HEADER_SIZE];

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

// RGB888 palette
static const uint32_t nes_palette_raw[64] = {
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400, 0x503000, 0x007800, 0x006800, 0x005800, 0x004058, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10, 0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0x000000, 0x000000, 0x000000,
    0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898, 0xF87858, 0xFCA044, 0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA4E4FC, 0xB8B8F8, 0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8, 0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xF8D8F8, 0x000000, 0x000000

};

const ines_header_t *INES = (ines_header_t *) CARTRIDGE;

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

static uint8_t *key_status;

M6502 cpu;

void HandleInput(WPARAM wParam, BOOL isKeyDown) {
}
uint16_t prg_rom_size = 0;
void print_header_info() {
    if (memcmp(INES->magic, "NES\x1A", 4) != 0) {
        fprintf(stderr, "Invalid iNES file %s!\n", INES->magic);
        return;
    }

    prg_rom_size = (INES->prg_rom_size * 16) << 10;
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

    fread(dst, sizeof(uint8_t), rom_size, file);
    fclose(file);
    return rom_size;
}

uint8_t Patch6502(register byte Op, register M6502 *R) {
}

/*
2000h - PPU Control Register 1 (W)
Bit7  Execute NMI on VBlank             (0=Disabled, 1=Enabled)
Bit6  PPU Master/Slave Selection        (0=Master, 1=Slave) (Not used in NES)
Bit5  Sprite Size                       (0=8x8, 1=8x16)
Bit4  Pattern Table Address Background  (0=VRAM 0000h, 1=VRAM 1000h)
Bit3  Pattern Table Address 8x8 Sprites (0=VRAM 0000h, 1=VRAM 1000h)
Bit2  Port 2007h VRAM Address Increment (0=Increment by 1, 1=Increment by 32)
Bit1-0 Name Table Scroll Address        (0-3=VRAM 2000h,2400h,2800h,2C00h)
(That is, Bit0=Horizontal Scroll by 256, Bit1=Vertical Scroll by 240)
*/
uint8_t ppu_control_register1 = 0;
/*
2001h - PPU Control Register 2 (W)
Bit7-5 Color Emphasis       (0=Normal, 1-7=Emphasis) (see Palettes chapter)
Bit4  Sprite Visibility     (0=Not displayed, 1=Displayed)
Bit3  Background Visibility (0=Not displayed, 1=Displayed)
Bit2  Sprite Clipping       (0=Hide in left 8-pixel column, 1=No clipping)
Bit1  Background Clipping   (0=Hide in left 8-pixel column, 1=No clipping)
Bit0  Monochrome Mode       (0=Color, 1=Monochrome)  (see Palettes chapter)
If both sprites and BG are disabled (Bit 3,4=0) then video output is disabled, and VRAM can be accessed at any time (instead of during VBlank only). However, SPR-RAM does no longer receive refresh cycles, and its content will gradually degrade when the display is disabled.
*/
uint8_t ppu_control_register2 = 0;

/*
2002h - PPU Status Register (R)
Bit7   VBlank Flag    (1=VBlank)
Bit6   Sprite 0 Hit   (1=Background-to-Sprite0 collision)
Bit5   Lost Sprites   (1=More than 8 sprites in 1 scanline)
Bit4-0 Not used       (Undefined garbage)
*/
uint8_t ppu_status_register = 0;
uint8_t ppu_read_buffer;


uint8_t ppu_latch = 0;
uint16_t ppu_address = 0;
uint8_t ppu_address_increment_step = 1;

uint16_t ppu_base_nametable_address = 0;
uint16_t ppu_sprite_pattern_address = 0;
uint16_t ppu_bg_pattern_address = 0;
uint8_t ppu_sprite_size = 8;

uint8_t ppu_nmi_enabled = 0;

enum {
    PPU_CTRL,
    PPU_MASK,
    PPU_STATUS,

    OAM_ADDR,
    OAM_DATA,

    PPU_SCROLL,

    PPU_ADDRESS,
    PPU_DATA,

    OAM_DMA = 0x14
};
uint8_t oam_address = 0;
uint8_t ppu_scroll_x, ppu_scroll_y;

uint8_t ppu_mirroring = 0;

static inline void vram_write(const uint16_t address, const uint8_t value) {
    if (address < 0x2000) {
        printf("!!! Writing CHR\n");
    } else if (address < 0x3F00) {
        VRAM[address & 1023] = value;
    } else {
        printf("!!! Writing palette %x %x ?\n", address, value );
        PALETTE_RAM[address & 63] = value;
        uint32_t pallete = nes_palette_raw[address & 63];
        // mfb_set_pallete(address & 63, MFB_RGB(pallete >> 0 & 0xff, pallete >> 8 & 0xff, pallete >> 16 & 0xff));
        mfb_set_pallete(address & 63, pallete);
    }
    ppu_address = (ppu_address + ppu_address_increment_step) & 0x3fff;
}

static inline void ppu_write(const uint16_t address, const uint8_t value) {
    // if ((address & 7) != 7) printf("ppu_write(%x, 0x%02x)\n", address, value);
    switch (address & 7) {
        case PPU_CTRL: // PPU Control Register 1
            ppu_control_register1 = value;

            ppu_base_nametable_address = (value & 7 << 10); // (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)

            ppu_address_increment_step = value & BIT_2 ? 32 : 1;
            ppu_sprite_pattern_address = (value & BIT_3) ? 0x1000 : 0x0000; // 0 - 0x0000, 1 - 0x1000 only for 8x8, 8x16 uses both
            ppu_bg_pattern_address = value & BIT_4 ? 0x1000 : 0x0000; // 0 - 0x0000, 1 - 0x1000
            ppu_sprite_size = value & BIT_5 ? 16 : 8;\
            if (ppu_sprite_size == 16) {
                printf("8x16 sprite pattern enabled\n");
            }
        // BIT_6 unused

            ppu_nmi_enabled = value & BIT_7 ? 1 : 0;
        /* Changing NMI enable from 0 to 1 while the vblank flag in PPUSTATUS is 1 will immediately trigger an NMI. This happens during vblank if the PPUSTATUS register has not yet been read. It can result in graphical glitches by making the NMI routine execute too late in vblank to finish on time, or cause the game to handle more frames than have actually occurred. To avoid this problem, it is prudent to read PPUSTATUS first to clear the vblank flag before enabling NMI in PPUCTRL. */

            break;
        case PPU_MASK: // PPU Control Register 2
            ppu_control_register2 = value;
            break;
        case PPU_SCROLL:
            if (ppu_latch ^= 1) {
                ppu_scroll_x = value;
            } else {
                ppu_scroll_y = value;
            }
            break;
        case PPU_ADDRESS: // VRAM Address Register
            if (ppu_latch ^= 1) {
                ppu_address &= 0xFF;
                ppu_address |= (value & 0x3F) << 8;
            } else {
                ppu_address &= 0xFF00;
                ppu_address |= value;
                // printf(">> set ppu_address = 0x%04X\n", ppu_address);
            }
            break;
        case PPU_DATA: // VRAM Read/Write Data Register
            vram_write(ppu_address, value);
            break;
        case OAM_ADDR:
            // printf("OAM address = 0x%04X\n", value);
            oam_address = value;
            break;
        case OAM_DATA:
            // printf("OAM data = 0x%04X\n", value);
            OAM[oam_address++] = value;
            break;
    }

}

static inline uint8_t vram_read(const uint16_t address) {
    if (address < 0x2000) {
        printf("!!! reading CHR\n");
    } else if (address < 0x3F00) {
        const uint8_t result = ppu_read_buffer;
        ppu_read_buffer = VRAM[ppu_address];
        ppu_address = (ppu_address + ppu_address_increment_step) & 0x3fff;
        return result;
    } else {
        printf("!!! reading palette?\n");
    }
}

static inline uint8_t ppu_read(const uint16_t address) {
    if (address & 7 != 2) printf("ppu_read(%x)\n", address);
    switch (address & 7) {
        case OAM_DATA:
            return OAM[oam_address];
        case PPU_STATUS: // PPU Status Register
            const uint8_t ppu_status = ppu_status_register;
            ppu_latch = 0;
            ppu_status_register &= ~BIT_7;
            return ppu_status;
        case PPU_DATA:
            return vram_read(ppu_address);
    }
    return 0xff;
}

uint8_t buttons = 0xff;

uint8_t Rd6502(uint16_t address) {
    // printf("Read 6502: %02x\n", address);
    if (address < 0x2000) {
        return RAM[address & (RAM_SIZE - 1)];
    }
    // PPU
    if (address < 0x4000) {
        return ppu_read(address);
    }

    // APU
    if (address < 0x4017) {
        switch (address & 0x17) {
            case 0x16: {
                uint8_t bit = buttons & 1;
                buttons >>= 1;
                return bit;
            }
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

    return ROM[(address - 0x8000) % prg_rom_size]; // do a trick with -0x8000 pointer
}

void Wr6502(uint16_t address, uint8_t value) {
    // printf("Write 6502: %02x\n", address);
    if (address < 0x2000) {
        RAM[address & (RAM_SIZE - 1)] = value;
        return;
    }

    if (address < 0x4000) {
        return ppu_write(address, value);
    }

    if (address == 0x4014) {
             // printf("!>!>!>!> oam_dma = %04x\n", value << 8);
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

uint16_t scanline = 0;

#define TILE_WIDTH 8
#define TILE_HEIGHT 8

static inline void dendy_frame() {
    scanline = 0;
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
    readfile(filename, CARTRIDGE);
    print_header_info();

    if (!mfb_open("Dendy", NES_WIDTH, NES_HEIGHT, scale))
        return 1;


    key_status = (uint8_t *) mfb_keystatus();

    // CreateThread(NULL, 0, SoundThread, NULL, 0, NULL);
    // CreateThread(NULL, 0, TicksThread, NULL, 0, NULL);

    Reset6502(&cpu);

    memset(RAM, 0, sizeof(RAM));
    memset(VRAM, 0, sizeof(VRAM));
    memset(SCREEN, 0, NES_WIDTH * NES_HEIGHT);


    while (1) {
        dendy_frame();

        if (mfb_update(SCREEN, 60) == -1)
            exit(1);
    }
}
