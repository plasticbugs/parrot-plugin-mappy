// mproms.h -- where the ROMs go: MAME's "mappy" set, each file known by
// its size and CRC-32, never by its name. Portable: the plugin and the Mac
// tests share it. (Time Pilot's tproms.h.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "mp.h"

typedef struct {
    const char *name;                   // MAME's name, for messages
    uint32_t size, crc;
    uint32_t at;                        // where its bytes go: in the store, or (MP_GFX_AT on) in the graphics' room
} mp_rom_file_t;

#define MP_ROM_FILES 11
#define MPROMS_ALL   ((1u << MP_ROM_FILES) - 1)
#define MP_GFX_AT    0x80000000u        // (in mp_rom_gfx, which mp_init decodes and then leaves alone)
#define MP_GFX_BYTES 0x5000             // the tiles, then the sprites' two halves
extern const mp_rom_file_t mp_rom_files[MP_ROM_FILES];

typedef struct {
    uint8_t maincpu[0x6000], sub[0x2000], proms[0x220], wave[0x100];
} mp_rom_store_t;
extern mp_rom_store_t mp_rom_store;
extern uint8_t *mp_rom_gfx;             // MP_GFX_BYTES of room for the graphics, set before loading

uint32_t mp_crc32(uint32_t crc, const uint8_t *p, uint32_t n);
// which of the files still to find (not in found) has this size and CRC-32 (-1: none)
int mproms_wanted(uint32_t size, uint32_t crc, uint32_t found);
bool mproms_size_ok(uint32_t size);     // one of the files' sizes
// a file's bytes (size of them, its CRC-32 right) to its place
void mproms_put(int i, const uint8_t *data);
// the board's ROMs, from the store and the graphics' room
void mproms_roms(mp_roms_t *r);
