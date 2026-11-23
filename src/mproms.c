// mproms.c -- see mproms.h. The sizes and CRC-32s are MAME 0.288's
// (namco/mappy.cpp's mappy). (Time Pilot's tproms.c.)
#include "mproms.h"

#include <stddef.h>
#include <string.h>

mp_rom_store_t mp_rom_store;
uint8_t *mp_rom_gfx;

#define S(f, o) (uint32_t)(offsetof(mp_rom_store_t, f) + (o))
#define G(o) (MP_GFX_AT | (o))
const mp_rom_file_t mp_rom_files[MP_ROM_FILES] = {
    {"mpx_3.1d", 0x2000, 0x52e6c708u, S(maincpu, 0)},
    {"mp1_2.1c", 0x2000, 0xa958a61cu, S(maincpu, 0x2000)},
    {"mpx_1.1b", 0x2000, 0x203766d4u, S(maincpu, 0x4000)},
    {"mp1_4.1k", 0x2000, 0x8182dd5bu, S(sub, 0)},
    {"mp1_5.3b", 0x1000, 0x16498b9fu, G(0)},
    {"mp1_6.3m", 0x2000, 0xf2d9647au, G(0x1000)},
    {"mp1_7.3n", 0x2000, 0x757cf2b6u, G(0x3000)},
    {"mp1-5.5b", 0x0020, 0x56531268u, S(proms, 0)},
    {"mp1-6.4c", 0x0100, 0x50765082u, S(proms, 0x20)},
    {"mp1-7.5k", 0x0100, 0x5396bd78u, S(proms, 0x120)},
    {"mp1-3.3m", 0x0100, 0x16a9166au, S(wave, 0)},
};
#undef S
#undef G

uint32_t mp_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                   0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                   0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

int mproms_wanted(uint32_t size, uint32_t crc, uint32_t found) {
    for (int i = 0; i < MP_ROM_FILES; i++)
        if (mp_rom_files[i].size == size && mp_rom_files[i].crc == crc && !(found & 1u << i)) return i;
    return -1;
}

bool mproms_size_ok(uint32_t size) {
    for (int i = 0; i < MP_ROM_FILES; i++)
        if (mp_rom_files[i].size == size) return true;
    return false;
}

void mproms_put(int i, const uint8_t *data) {
    const mp_rom_file_t *f = &mp_rom_files[i];
    uint8_t *to = f->at & MP_GFX_AT ? mp_rom_gfx + (f->at & ~MP_GFX_AT) : (uint8_t *)&mp_rom_store + f->at;
    memmove(to, data, f->size);
}

void mproms_roms(mp_roms_t *r) {
    r->maincpu = mp_rom_store.maincpu;
    r->sub = mp_rom_store.sub;
    r->tiles = mp_rom_gfx;
    r->sprites0 = mp_rom_gfx + 0x1000;
    r->sprites1 = mp_rom_gfx + 0x3000;
    r->proms = mp_rom_store.proms;
    r->wave = mp_rom_store.wave;
}
