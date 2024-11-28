#include "ppu.h"

enum {
    PPU_CTRL,
    PPU_MASK,
    PPU_STATUS,

    OAM_ADDR,
    OAM_DATA,

    PPU_SCROLL,

    PPU_ADDRESS,
    PPU_DATA,

    OAM_DMA = 0x4014
};

PPU ppu = { 0 };

static uint8_t ppu_latch = 0;
static uint8_t ppu_read_buffer = 0;
static uint8_t oam_address = 0;

uint8_t VRAM[2048] = { 0 };
uint8_t OAM[256] = { 0 };
uint8_t PALETTE[64] = { 0 };

static inline void increment_address() {
    ppu.address = ppu.address + ppu.address_step & 0x3fff;
}

static inline void vram_write(const uint16_t address, const uint8_t value) {
    if (address < 0x2000) {
        debug_log("!!! Writing CHR\n");
    } else if (address < 0x3F00) {
        VRAM[ppu.mirroring ? address & 2047 : address / 2 & 1024 | address % 1024] = value;
        increment_address();
    } else {
        // printf("!!! Writing palette %x %x ?\n", address, value);
        PALETTE[address - 0x3F00] = value;
    }

}


void ppu_write(const uint16_t address, const uint8_t value) {
    // printf("ppu_write %x %x\n", address, value);
    switch (address & 7) {
        case PPU_CTRL:
            ppu.nametable = &VRAM[value & 0b111 << 10 & 2047]; // (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)

            ppu.scroll_x |= value & 1 << 8;
            ppu.scroll_y |= value >> 1 & 1 << 8;

            ppu.address_step = value & BIT_2 ? 32 : 1;
            ppu.sprite_height = value & BIT_5 ? 16 : 8;

            ppu.sprites = &ppu.chr_rom[ppu.sprite_height == 8 && value & BIT_3 ? 0x1000 : 0x0000];
            ppu.background = &ppu.chr_rom[value & BIT_4 ? 0x1000 : 0x0000];

            ppu.nmi_enabled = value & BIT_7 ? 1 : 0;
            break;
        case PPU_MASK:
            ppu.background_enabled = value & BIT_3 ? 1 : 0;
            ppu.sprites_enabled = value & BIT_4 ? 1 : 0;
            break;
        case PPU_SCROLL:
            if (ppu_latch ^= 1) {
                ppu.scroll_x |= value;
            } else {
                ppu.scroll_y |= value;
            }
            break;
        case PPU_ADDRESS: // VRAM Address Register
            if (ppu_latch ^= 1) {
                ppu.address &= 0xFF;
                ppu.address |= (value & 0x3F) << 8;
            } else {
                ppu.address &= 0xFF00;
                ppu.address |= value;
            }
            break;
        case PPU_DATA: // VRAM Read/Write Data Register
            vram_write(ppu.address, value);
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
        return ppu.chr_rom[address];
    }

    if (address < 0x3F00) {
        const uint8_t result = ppu_read_buffer;
        ppu_read_buffer = VRAM[ppu.address];
        increment_address();
        return result;
    }

    debug_log("!!! reading palette?\n");
}

uint8_t ppu_read(const uint16_t address) {
    // printf("ppu_read(%x)\n", address);
    switch (address & 7) {
        case PPU_STATUS: // PPU Status Register
            const uint8_t ppu_status = ppu.status;
            ppu_latch = 0;
            ppu.status &= ~BIT_7;
            return ppu_status;
        case PPU_DATA:
            return vram_read(ppu.address);
        case OAM_DATA:
            return OAM[oam_address];
    }
    return 0xff;
}
