// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles
//
// mp.h -- the Mappy board (Namco, 1983) as MAME 0.288 emulates it
// (namco/mappy.cpp, mappy_v.cpp, namcoio.cpp, sound/namco.cpp, 74259.cpp,
// screen.cpp): two MC6809Es, the two 58XX I/O chips, the LS259 latch and
// the 15XX sound chip's registers, run on the kit's lib/m6809 and
// lib/sched so both CPUs run the cycles MAME's do. The 15XX's voices are
// mpsnd.c: this file hands it each register write with its time.
// Portable: the plugin and the Mac tests share it.
#ifndef MP_BOARD_H
#define MP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define MP_CLOCK 1536000u               // both 6809Es: 18.432 MHz / 3 / 4
#define MP_SND_RATE 192000u             // the 15XX's stream (24 kHz, doubled to 192 kHz in MAME)
#define MP_W 288                        // the raw screen, before MAME's ROT90
#define MP_H 224

typedef struct {
	const uint8_t *maincpu;             // 24K, 0xa000-0xffff: mpx_3, mp1_2, mpx_1
	const uint8_t *sub;                 // 8K, 0xe000-0xffff: mp1_4
	const uint8_t *tiles;               // 4K: mp1_5 (as on the chip; MAME inverts it)
	const uint8_t *sprites0;            // 8K: mp1_6 (the even bytes)
	const uint8_t *sprites1;            // 8K: mp1_7 (the odd bytes)
	const uint8_t *proms;               // 0x220: mp1-5 (palette), mp1-6 (tiles), mp1-7 (sprites)
	const uint8_t *wave;                // 0x100: mp1-3, the 15XX's waveforms
} mp_roms_t;

// the ports as MAME has them (active low, 4 bits each). MAME's defaults:
// DSW0 0x0f (upright), DSW1 0xff (rank A, coin B 1/1, demo sounds on),
// DSW2 0xff (coin A 1/1, bonus 20k 70k, 3 lives)
typedef struct {
	uint8_t coins;                      // 0x01 coin 1, 0x02 coin 2, 0x08 service
	uint8_t p1;                         // 0x02 right, 0x08 left
	uint8_t p2;                         // (cocktail) the same
	uint8_t buttons;                    // 0x01 P1 button, 0x02 P2's, 0x04 start 1, 0x08 start 2
	uint8_t dsw0, dsw1, dsw2;
} mp_inputs_t;
#define MP_DSW0 0x0f
#define MP_DSW1 0xff
#define MP_DSW2 0xff
extern mp_inputs_t mp_in;

void mp_init(const mp_roms_t *roms);   // power on and the machine's reset

// Run while mp_time() is before `until` (the main CPU's cycles from power
// on), stopping at the first of MAME's timeslice ends at or past it.
// Returns the VBLANKs passed; mp_on_vblank, if set, is called at each
// (where MAME's frame_update draws the screen and calls Lua's frame_done).
uint32_t mp_run_to(uint64_t until);
uint64_t mp_time(void);                 // where the board is, in main CPU cycles (1.536 MHz)
void mp_frame(void);                    // run to the next VBLANK
extern void (*mp_on_vblank)(void);

// RAM as the main CPU sees it
extern uint8_t mp_mem[0x2800];          // 0x0000: tile codes (0x800) and attributes (0x800), then work RAM with the sprites
extern uint8_t mp_snd[0x400];           // the 15XX: its registers (0x00-0x3f) and the shared RAM (0x4000, the sub CPU's 0x0000)
extern uint8_t mp_io[2][16];            // the 58XXs' RAM (4 bits each)
extern uint8_t mp_scroll;               // the 17XX's scroll (0x3800-0x3fff written: offset >> 3)
extern uint8_t mp_latch;                // the LS259 (2M): bit n is Q n
extern uint32_t mp_frames;              // VBLANKs so far
extern uint32_t mp_watchdog_fired;      // (tests) the watchdog would have reset the board
extern uint32_t mp_tile_dirty[0x800 / 32];   // a bit for each tile whose code or attribute was written changed (the picture clears them)
#define MP_VIDEORAM (mp_mem)
#define MP_SPRITERAM (mp_mem + 0x1000)  // m_spriteram: its 0x780, 0xf80, 0x1780 hold the sprites
#define MP_FLIP (mp_latch & 0x04)
#define MP_SOUND_ON (mp_latch & 0x08)

// The screen as MAME draws it at a VBLANK (screen_update_mappy): 288 x
// 224 raw pens: 0-255 the tiles' (colour * 4 + pixel), 256-511 the
// sprites' (256 + colour * 16 + pixel); mp_rgb[] is each as MAME's palette
// makes it. Tests only (the cart's picture is mpcv.c's): not built for the cart.
void mp_render(uint16_t *px);
// mappy_tilemap_scan: the video RAM offset of the tilemap's column (0-35,
// raw x / 8) and row (0-59, raw y / 8 through the scroll); 0x7ff off screen
int mp_tile_index(int col, int row);
extern uint32_t mp_rgb[512];            // 0xRRGGBB: the pens through the PROMs
extern uint32_t mp_pal[32];             // the 32 colours (mp1-5 through MAME's resistors)
extern uint8_t mp_tile_ctab[256];       // a tile pen's colour (0-15 of the upper 16: 16-31)
extern uint8_t mp_spr_ctab[256];        // a sprite pen's colour (0-15; 15 is see-through)
// the graphics, decoded: a tile's row, 8 pixels of 2 bits (x at bits 2x);
// a sprite's row, 16 pixels of 4 bits (x at bits 4x, in two words)
extern uint16_t mp_tile_rows[256][8];
extern uint64_t mp_sprite_rows[128][16];

// sound: each 15XX register write with its time in the stream's ticks
// (192 kHz from power on), and the sound enable (the latch's Q3). The
// board calls these as MAME's 15XX gets them (mpsnd.c).
void mp_snd_write(uint64_t tick, int reg, uint8_t value);
void mp_snd_enable(uint64_t tick, int on);
uint64_t mp_snd_now(void);              // the stream's tick now

// for the tests
uint64_t mp_cycles(int cpu);            // 0: the main CPU's cycles since its reset, 1: the sub CPU's
void mp_regs(int cpu, uint16_t r[6]);   // PC S U X Y D, then
uint8_t mp_regs8(int cpu, int which);   // 0 DP, 1 CC
uint8_t mp_peek(int cpu, uint16_t a);   // as the CPU reads, without side effects
#ifdef MP_TRACE
extern void (*mp_trace)(int cpu, uint64_t cycles);   // (built with MP_TRACE) before each instruction: its CPU's total cycles
#endif

#endif
