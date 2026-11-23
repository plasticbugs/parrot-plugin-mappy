// mpcv: the cart's side of Mappy on the ColecoVision. Portable C (no
// pico-sdk), used by the plugin and by the Mac tests.
//
// The console runs the SDK's frame kernel (gbkernel.asm): each TV frame it
// takes a command list from the cart and runs it. This makes those lists
// from the board (mp.h) and its sound (mpsnd.h):
//
//   once:                         mpcv_init(e)
//   at each VBLANK the cart uses: mpcv_picture(e, board)   (or mpcv_canvas for a message)
//   each list:                    mpcv_sound_pcm(e, ...)
//                                 len = mpcv_tv_frame(e, list)
//
// The picture. MAME shows the board's 288 x 224 turned (ROT90): 224 wide
// and 288 high, 36 rows of 28 tiles. Rows 2 to 33 (224 x 256) are the
// mansion, which scrolls sideways: a tilemap 480 pixels wide, 224 of them
// on the screen. The mansion is scaled as a whole, 5 columns of every 7
// and 3 lines of every 4 (480 x 256 to 343 x 192), and the console shows
// 160 columns of it, from x 48 to 207 (whole 8-pixel places, so the
// margins share none with it). The 7s and 4s are the mansion's own, not
// the screen's, so a floor or a door keeps its look while the screen
// scrolls. Two lines or columns become one the way sdk/cvscale.h does it
// (a stroke one pixel wide survives). The sprites are scaled one by one
// (16 x 16 to 12 x 12), so they keep their shape as they move, and put
// where the mansion's scaling puts their middle.
//
// The scores, the lives, the rounds and the credits (rows 0, 1, 34 and
// 35) go to the 48-pixel margins at the arcade's size, every letter as
// the arcade draws it (mpcv.c, pieces[]).
//
// A list carries the bytes that differ from what the console holds, as
// many as the list has time for.
//
// The sound: samples through the SN76489's three tones parked at a period
// of 1, their attenuators summed as a DAC, MPCV_PCM_N a list with the
// console running the lists back to back (porting.md, "Lists back to back").
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp.h"

#define MPCV_W      256
#define MPCV_H      192
#define MPCV_BYTES  6144            // pattern bytes, and as many colour bytes
#define MPCV_LIST_MAX 3000
#define MPCV_PCM_N  100             // samples a list
// a list's length in the console's T (3,579,545 a second), by gbkernel.asm's
// header: 500 over a frame. MAME runs a sound byte a T faster than the
// header counts, and a list has 300 to 390 of them, so by MAME's costs the
// shortest list still runs about 140 T over a frame and the console never waits
// (porting.md, "Lists back to back")
#define MPCV_LIST_T 60236
#define MPCV_CONSOLE_HZ 3579545u
// the board's cycles (1.536 MHz) in a list, << 16
#define MPCV_LIST_CYCLES_Q16 ((uint64_t)MPCV_LIST_T * MP_CLOCK * 65536u / MPCV_CONSOLE_HZ)
// the samples' rate, << 8: MPCV_PCM_N a list (5,962 a second)
#define MPCV_RATE_Q8 ((uint32_t)((uint64_t)MPCV_PCM_N * MPCV_CONSOLE_HZ * 256u / MPCV_LIST_T))

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define MPCV_HOT __attribute__((section(".time_critical.mpcv")))
#else
#define MPCV_HOT
#endif

typedef struct {
    uint32_t tv_frames, pictures, pat_bytes, col_bytes, runs, refreshed, pcm_frames;
    uint32_t settled;               // lists that ended with nothing left to send
    uint32_t t_max;                 // the longest list, in T
    uint32_t tiles;                 // the mansion's tiles scaled again
    uint32_t cells;                 // 8 x 1 places made again
    uint32_t scrolls;               // pictures whose view of the mansion moved
    uint32_t clipped;               // samples the soft knee took in
} mpcv_stats_t;

typedef struct {
    // the console's VRAM as the lists so far leave it, and the newest picture
    uint8_t vram_pat[MPCV_BYTES], vram_col[MPCV_BYTES];
    uint8_t want_pat[MPCV_BYTES], want_col[MPCV_BYTES];
    bool names_done;
    uint16_t names_at;
    uint16_t cursor;                // where the next list's scan starts
    uint16_t refresh_at;
    bool dirty;                     // want differs from vram somewhere (or may)
    uint32_t bands;                 // the same, for each band of 8 lines (256 bytes)

    // the picture as it was made last
    bool pic_valid;
    int16_t win;                    // the scaled mansion's column at x 48
    uint8_t vram_seen[0x1000];      // the board's tile codes and attributes it was drawn from
    uint32_t cells_prev[24];        // the 8 x 8 places the sprites were in

    // the sound
    bool pcm;                       // the lists carry samples
    uint8_t pcm_gain;               // 0-32: the DAC comes up over the first lists
    uint8_t pcm_bytes[MPCV_PCM_N][3];  // each sample's three attenuators, in the order written
    uint8_t dac_att[3];             // the attenuators as the last sample left them
    uint8_t tone_bytes[12];
    uint8_t tone_n;
    uint8_t resync;
    mpcv_stats_t stats;
} mpcv_t;

void mpcv_init(mpcv_t *e);
// the colours of the board's pens and the scaling's maps, from its PROMs
// (after mp_init), before the first picture
void mpcv_prepare(void);
// room the plugin may use before the first mpcv_picture (a message's
// canvas, the card's reads); mpcv_canvas and the sound leave it alone
uint8_t *mpcv_scratch(size_t *n);

// what a picture is made from: the board's tile RAM, sprites and scroll
// at a VBLANK (where MAME draws its screen)
typedef struct {
    uint8_t vram[0x1000];               // tile codes (0x000-0x7ff), attributes (0x800-0xfff)
    uint8_t spr[3][0x80];               // the sprite RAM's 0x780, 0xf80 and 0x1780 (MAME's spriteram 1-3)
    uint8_t scroll;
    uint32_t frame;                     // the VBLANK it was taken at (mp_frames; the tests)
} mpcv_board_t;
void mpcv_board(mpcv_board_t *b);       // the board as it is (at its VBLANK)
// a new picture of b
void mpcv_picture(mpcv_t *e, const mpcv_board_t *b);
// a new picture: 256 x 192 pixels, a TMS9918 colour each (1-15); for messages
void mpcv_canvas(mpcv_t *e, const uint8_t *px);

// The next list's sound: n samples at MPCV_RATE_Q8 in the DAC's levels
// either side of its rest, 64 a level (mpsnd.h; n = 0: none, a message: the
// console waits for the vertical blank again, and the DAC is let go).
void mpcv_sound_pcm(mpcv_t *e, const int16_t *s, int n);

// the next list into out (MPCV_LIST_MAX bytes); returns its length
size_t mpcv_tv_frame(mpcv_t *e, uint8_t *out);

// the picture the console shows once the lists so far have run, a TMS9918
// colour a pixel (tests, previews); the picture before the TMS9918's two
// colours a place (every pixel its own colour)
void mpcv_screen(const mpcv_t *e, uint8_t *px);
void mpcv_ideal(const mpcv_t *e, uint8_t *px);
// bytes the console still needs (tests; a whole scan)
uint32_t mpcv_backlog(const mpcv_t *e);
// the DAC's level for the three tones' attenuators (tests: the lists' sound as a WAV)
float mpcv_dac_level(int att0, int att1, int att2);
// the TMS9918's colours as 0xRRGGBB (previews)
extern const uint32_t mpcv_tms_rgb[16];
// the board's pens as TMS9918 colours (tests): a tile's (0-255), a sprite's (0-255, 0 see-through)
extern uint8_t mpcv_tile_tms[256], mpcv_spr_tms[256];
// a text tile's letter as the mansion shows it, 6 x 6 (bit x of row y),
// and the pairs of its columns and of its lines made one (bit j: j and
// j + 1; none: drawn, the copyright sign); false for a tile that isn't
// text (tests: test/mp/textcheck.c)
bool mpcv_text_glyph(int code, uint8_t rows[6], uint8_t *cols, uint8_t *lines);

// the pairs of a sprite's columns and of its lines made one, unflipped
// (bit j: j and j + 1); its own for a sprite of one or two pens, the
// points and the like, else 0x2222 (tests: test/mp/textcheck.c)
bool mpcv_spr_glyph(int code, uint16_t *cols, uint16_t *lines);
