// mpcv.c -- see mpcv.h. The lists, the DAC, the two-colour picker, the
// sprites' scaling and the margins' pieces are Time Pilot's (Parrot Cart's
// parrot-plugin-timeplt, src/tpcv.c, itself from Donkey Kong's, Pac-Man's
// and Q*bert's); the mansion scaled as a whole, the window on it and the
// margins' layout are new.
#include "mpcv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"
#include "mpdac3.h"

// ---- lists (from gbkernel.asm's costs) -----------------------------------------------
#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    480
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

// The console goes from one list straight into the next, so a list's
// length is the sample clock
#define C_FRAME    357
#define PCM_L      MPCV_LIST_T
#define PCM_FIRST  (C_FRAME + C_END_VB)
#define C_SAMPLE   (C_PSG + 3 * C_PSG_BYTE)
#define PCM_STEP   (((PCM_L - PCM_FIRST - C_SAMPLE - C_END) << 8) / (MPCV_PCM_N - 1))

#define PATTERNS 0x0000
#define COLORS   0x2000
#define NAMES    0x1800
#define BLACK    1

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static void l_run_head(list_t *l, uint16_t addr, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    l->t += C_PSG + C_PSG_BYTE * n;
    l->b[l->n++] = 2;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

static int room(const list_t *l) {
    int t = l->vblank ? ((int)VB_LIMIT - (int)l->t - C_END_VB - C_RUN_VB) / C_BYTE_VB
                      : ((int)LIST_LIMIT - (int)l->t - C_END - C_RUN_ACT) / C_BYTE_ACT;
    return t > 255 ? 255 : t;
}

// ---- colours ------------------------------------------------------------------------
const uint32_t mpcv_tms_rgb[16] = {0x000000, 0x000000, 0x21C842, 0x5EDC78, 0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
                                   0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80, 0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF};

static float tms_lab[16][3];
static uint16_t nearer[16][16];         // [c][a] bit b: c is nearer a than b
#define NEARER(c, a, b) ((nearer[c][a] >> (b)) & 1)
static bool colors_ready;

static void oklab(uint32_t rgb, float *out) {
    float c[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((rgb >> (16 - 8 * i)) & 255) / 255.0f;
        c[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    float l = cbrtf(0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2]);
    float m = cbrtf(0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2]);
    float s = cbrtf(0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2]);
    out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static float d2(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

static void colors_init(void) {
    for (int i = 1; i < 16; i++) oklab(mpcv_tms_rgb[i], tms_lab[i]);
    for (int c = 1; c < 16; c++)
        for (int a = 1; a < 16; a++)
            for (int b = 1; b < 16; b++)
                if (d2(tms_lab[c], tms_lab[a]) <= d2(tms_lab[c], tms_lab[b])) nearer[c][a] |= (uint16_t)(1u << b);
    colors_ready = true;
}

// Mappy's 32 colours (mp_pal) as the TMS9918's: the nearest in OKLab, a
// grey only to black, grey or white, except where the eye says otherwise
// (checked against MAME's pictures, test/mp/colors.sh)
static uint8_t nearest(uint32_t rgb) {
    float lab[3];
    oklab(rgb, lab);
    bool grey = lab[1] * lab[1] + lab[2] * lab[2] < 0.03f * 0.03f;
    uint8_t best = 1;
    float bd2 = 1e9f;
    for (int t = 1; t < 16; t++) {
        if (grey && t != 1 && t != 14 && t != 15) continue;
        float d = d2(lab, tms_lab[t]);
        if (d < bd2) { bd2 = d; best = (uint8_t)t; }
    }
    return best;
}

static const struct { uint32_t rgb; uint8_t tms; } picked[] = {
    {0xFF97FFu, 13},                    // the doors' pink: magenta (the nearest is grey)
    {0x00B8AEu, 7},                     // the safe's teal: cyan (the nearest is dark green)
    {0xDEDEFFu, 15},                    // Mappy's white: white (the nearest is grey)
};

static uint8_t tms_of(uint32_t rgb) {
    for (size_t i = 0; i < sizeof picked / sizeof picked[0]; i++)
        if (picked[i].rgb == rgb) return picked[i].tms;
    return nearest(rgb);
}

// The board's pens (mp_rgb: the PROMs through MAME's resistors) as TMS9918
// colours; a tile's pen in front of the sprites (a category 1 tile's, not
// see-through) has 0x80 too
uint8_t mpcv_tile_tms[256], mpcv_spr_tms[256];
static uint8_t tile_front[256];
static uint8_t tile_blank[256];          // a bit a line (y % 8) of each tile: all of it pen 0
static bool pens_ready;

static void pens_init(void) {
    if (!colors_ready) colors_init();
    for (int i = 0; i < 256; i++) {
        mpcv_tile_tms[i] = tms_of(mp_rgb[i]);
        tile_front[i] = mp_tile_ctab[i] != 15 ? 0x80 : 0;
        mpcv_spr_tms[i] = mp_spr_ctab[i] == 15 ? 0 : tms_of(mp_rgb[256 + i]);
    }
    for (int code = 0; code < 256; code++) {
        uint16_t any = 0;
        for (int r = 0; r < 8; r++) any |= mp_tile_rows[code][r];
        uint8_t m = 0;
        for (int y = 0; y < 8; y++)
            if (!((any >> (2 * y)) & 3)) m |= (uint8_t)(1u << y);
        tile_blank[code] = m;
    }
    pens_ready = true;
}

// ---- the picture's state -------------------------------------------------------------
// The mansion: MAME's tiles turned, 480 x 256 (the world: its column U is
// the tilemap's, so the screen's column X is U - (256 - scroll)), scaled
// to 343 x 192 as TMS9918 colours (bit 7: in front of the sprites). Only
// the scaled columns from range_lo to range_hi are kept up to date: those
// the console has shown since the last whole picture. The margins: x 0-47
// and 208-255 (MX). The sprites' images are kept in scache.
#define PF_X   48                       // the play area on the TMS9918's screen
#define PF_W   160
#define PF_H   192
#define WU     480                      // the mansion's columns
#define WS     343                      // scaled
#define PL     256                      // its lines (the screen's rows 2-33)
#define MW     (MPCV_W - PF_W)
#define MX(x)  ((x) < PF_X ? (x) : (x) - PF_W)

typedef struct {
    uint32_t key;                       // what it was made from (0: nothing)
    uint32_t stamp;                     // the last picture that used it
    uint8_t px[12][12];                 // TMS9918 colours, 0 where it has none
} simg_t;

typedef struct {
    int16_t x, y;                       // its 12 x 12's top left on the TMS9918's screen
    int16_t x0, y0, x1, y1;             // that, clipped
    const simg_t *img;
} vis_t;

#define SCACHE 64
static union {
    struct {
        uint8_t wpen[PF_H][WS + 1];
        uint8_t margin[MPCV_H][MW];
        simg_t scache[SCACHE];
        uint32_t ov_hash[24][32];
    } p;
    uint8_t room[100 * 1024];
    uint64_t align;
} pic_u;
#define wpen    pic_u.p.wpen
#define margin  pic_u.p.margin
#define scache  pic_u.p.scache
#define ov_hash pic_u.p.ov_hash
_Static_assert(sizeof pic_u.p <= sizeof pic_u.room, "the picture's state in its room");
static bool pic_clear;
static int range_lo = 1, range_hi = 0;  // the scaled columns up to date (none)
static int cur_win;                     // e->win, for rescale's marks

static vis_t vis[96];
static int nvis;

uint8_t *mpcv_scratch(size_t *n) {
    *n = sizeof pic_u.room;
    pic_clear = false;
    return pic_u.room;
}

// ---- the 8 x 8 places to make again ----------------------------------------------------
static uint32_t cells[24];              // a bit a column, a word a band

static void mark(int x0, int y0, int x1, int y1) {     // pixels x0..x1 - 1, y0..y1 - 1
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > MPCV_W) x1 = MPCV_W;
    if (y1 > MPCV_H) y1 = MPCV_H;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t m = (x1 + 7) / 8 >= 32 ? ~0u : (1u << ((x1 + 7) / 8)) - 1;
    m &= ~((1u << (x0 / 8)) - 1);
    for (int b = y0 / 8; b <= (y1 - 1) / 8; b++) cells[b] |= m;
}

static void mark_play(void) { mark(PF_X, 0, PF_X + PF_W, PF_H); }

// ---- the mansion's scaling: 5 columns of 7, 3 lines of 4 -------------------------------
// Scaled column k (0-342) is the mansion's column scol[k] (and the one
// after it, for two made one); line r (0-191) is its line srow[r]
static uint16_t scol[WS], col_of[WU];
static uint8_t spair_c[WS];
static uint8_t srow[PF_H], spair_r[PF_H], row_of[PL];
static bool maps_ready;

static void maps_init(void) {
    static const uint8_t c7[5] = {0, 1, 3, 4, 6}, p7[5] = {0, 1, 0, 1, 0};
    for (int k = 0; k < WS; k++) {
        scol[k] = (uint16_t)(7 * (k / 5) + c7[k % 5]);
        spair_c[k] = p7[k % 5];
        col_of[scol[k]] = (uint16_t)k;
        if (spair_c[k]) col_of[scol[k] + 1] = (uint16_t)k;
    }
    for (int r = 0; r < PF_H; r++) {
        srow[r] = (uint8_t)(4 * (r / 3) + (r % 3 == 2 ? 3 : r % 3));
        spair_r[r] = r % 3 == 1;
        row_of[srow[r]] = (uint8_t)r;
        if (spair_r[r]) row_of[srow[r] + 1] = (uint8_t)r;
    }
    maps_ready = true;
}

static const mpcv_board_t *bd;          // the board the picture is made from

// A mansion's line as TMS9918 colours (and the front bit), columns x0 to
// x1, drawn from the board's tile RAM. The mansion's column U is the
// tilemap's row 59 - U / 8 (its pixel line 7 - U % 8), its line y the
// tilemap's column 2 + y / 8 (pixel y % 8): mappy_tilemap_scan, turned.
static void world_line(int y, int x0, int x1, uint8_t *o) {
    int sh = 2 * (y & 7);
    for (int tc = x0 >> 3; tc <= x1 >> 3; tc++) {
        int i = (y >> 3) + ((59 - tc) << 5);
        uint8_t attr = bd->vram[i + 0x800];
        const uint8_t code = bd->vram[i];
        const uint16_t *rows = mp_tile_rows[code];
        const uint8_t *tms = &mpcv_tile_tms[(attr & 0x3f) * 4];
        const uint8_t *front = &tile_front[(attr & 0x3f) * 4];
        const uint8_t fm = (attr & 0x40) ? 0x80 : 0;
        uint8_t *q = o + tc * 8;
        if (tile_blank[code] >> (y & 7) & 1) {          // (most of the mansion: one colour)
            const uint32_t w = (uint32_t)(uint8_t)(tms[0] | (front[0] & fm)) * 0x01010101u;
            memcpy(q, &w, 4);
            memcpy(q + 4, &w, 4);
            continue;
        }
        const uint8_t c4[4] = {(uint8_t)(tms[0] | (front[0] & fm)), (uint8_t)(tms[1] | (front[1] & fm)),
                               (uint8_t)(tms[2] | (front[2] & fm)), (uint8_t)(tms[3] | (front[3] & fm))};
        q[0] = c4[(rows[7] >> sh) & 3];
        q[1] = c4[(rows[6] >> sh) & 3];
        q[2] = c4[(rows[5] >> sh) & 3];
        q[3] = c4[(rows[4] >> sh) & 3];
        q[4] = c4[(rows[3] >> sh) & 3];
        q[5] = c4[(rows[2] >> sh) & 3];
        q[6] = c4[(rows[1] >> sh) & 3];
        q[7] = c4[(rows[0] >> sh) & 3];
    }
}

// a line's scaled columns k0 to k1 from its pixels: 5 of every 7, the
// middle two pairs made one each (0, 1+2, 3, 4+5, 6)
static void scale_across(const uint8_t *row, int k0, int k1, uint8_t *o) {
    int k = k0;
    for (; k <= k1 && k % 5; k++) {
        int x = scol[k];
        o[k] = spair_c[k] ? cvscale_pick(row[x], row[x + 1], row[x - 1], row[x + 2]) : row[x];
    }
    for (; k + 4 <= k1; k += 5) {
        const uint8_t *q = row + scol[k];
        o[k] = q[0];
        o[k + 1] = cvscale_pick(q[1], q[2], q[0], q[3]);
        o[k + 2] = q[3];
        o[k + 3] = cvscale_pick(q[4], q[5], q[3], q[6]);
        o[k + 4] = q[6];
    }
    for (; k <= k1; k++) {
        int x = scol[k];
        o[k] = spair_c[k] ? cvscale_pick(row[x], row[x + 1], row[x - 1], row[x + 2]) : row[x];
    }
}

// the scaled columns k0..k1, lines r0..r1, from the board's tiles now. The
// lines go in groups of 4 to 3 (0, 1+2, 3), so each of a group's lines is
// drawn and scaled across once, then the group's rows are made from them.
static uint8_t src_line[WU];
static uint8_t across[4][WS + 1];
static void rescale(int k0, int r0, int k1, int r1) {
    if (k0 < 0) k0 = 0;
    if (r0 < 0) r0 = 0;
    if (k1 > WS - 1) k1 = WS - 1;
    if (r1 > PF_H - 1) r1 = PF_H - 1;
    if (k0 > k1 || r0 > r1) return;
    int x0 = scol[k0] - 1, x1 = scol[k1] + 2;   // (a pair's columns, and those either side)
    if (x0 < 0) x0 = 0;
    if (x1 > WU - 1) x1 = WU - 1;
    const int n = k1 - k0 + 1;
    for (int g = r0 / 3; g <= r1 / 3; g++) {
        const int ra = g * 3 > r0 ? g * 3 : r0, rb = g * 3 + 2 < r1 ? g * 3 + 2 : r1;
        const bool mid = ra <= g * 3 + 1 && rb >= g * 3 + 1;     // the row of two lines: all 4
        for (int j = 0; j < 4; j++) {
            if (!mid && !(j == 0 && ra == g * 3) && !(j == 3 && rb == g * 3 + 2)) continue;
            world_line(4 * g + j, x0, x1, src_line);
            scale_across(src_line, k0, k1, across[j]);
        }
        if (ra == g * 3) memcpy(&wpen[g * 3][k0], &across[0][k0], (size_t)n);
        if (mid) {
            uint8_t *o = wpen[g * 3 + 1];
            const uint8_t *u = across[1], *v = across[2], *bf = across[0], *af = across[3];
            for (int k = k0; k <= k1; k++) o[k] = cvscale_pick(u[k], v[k], bf[k], af[k]);
        }
        if (rb == g * 3 + 2) memcpy(&wpen[g * 3 + 2][k0], &across[3][k0], (size_t)n);
    }
    int s0 = PF_X + k0 - cur_win, s1 = PF_X + k1 + 1 - cur_win;
    if (s0 < PF_X) s0 = PF_X;
    if (s1 > PF_X + PF_W) s1 = PF_X + PF_W;
    if (s0 < s1) mark(s0, r0, s1, r1 + 1);
}

// the scaled column at x 48 for the board's scroll: the screen's middle
// (column 112, the mansion's 368 - scroll) at the play area's
static int window_of(int scroll) {
    int w = col_of[368 - scroll] - PF_W / 2;
    return w < 0 ? 0 : w > WS - PF_W ? WS - PF_W : w;
}

// ---- the margins' pieces -------------------------------------------------------------
// Rows 0, 1, 34 and 35 of MAME's (turned) tiles, put in the margins at the
// arcade's size, so every letter keeps its shape. Left: 1UP and its score,
// the high score, the lives, the credits. Right: 2UP and its score, the
// round markers (the arcade puts them from the right, so they're kept
// against the right edge). A row 1 score is 7 tiles, 56 pixels: below a
// million the first is blank and the other 6 fill the margin's 48; from a
// million on each tile loses a column (MPCV_SQUEEZE_AT and the one after
// it made one, which keeps every stroke and gap of all ten digits,
// test/mp/hudcheck.c). The lives and the rounds are two rows tall (16 x
// 16 badges, 8 x 16 markers), 6 columns at a time. Row 35 says CREDIT in
// the attract mode, where a game has the lives: the CREDIT pieces show
// only then, and the lives' pieces only when it doesn't.
enum { HUD_TEXT, HUD_SCORE, HUD_LIVES, HUD_CREDIT };
typedef struct { uint8_t c0, r0, nc, nr; uint8_t x, y, kind; } piece_t;
static const piece_t pieces[] = {
    {3, 0, 3, 1, 12, 2, HUD_TEXT},              // "1UP"
    {0, 1, 7, 1, 0, 11, HUD_SCORE},             // its score
    {9, 0, 4, 1, 8, 26, HUD_TEXT},              // "HIGH"
    {14, 0, 5, 1, 4, 35, HUD_TEXT},             // "SCORE"
    {10, 1, 7, 1, 0, 44, HUD_SCORE},            // the high score
    {0, 34, 6, 2, 0, 62, HUD_LIVES},            // the lives
    {6, 34, 6, 2, 0, 80, HUD_LIVES},            // (more of them)
    {2, 35, 6, 1, 0, 172, HUD_CREDIT},          // "CREDIT"
    {8, 35, 3, 1, 12, 182, HUD_CREDIT},         // the credits
    {22, 0, 3, 1, 220, 2, HUD_TEXT},            // "2UP"
    {19, 1, 7, 1, 208, 11, HUD_SCORE},          // its score
    {22, 34, 6, 2, 208, 62, HUD_TEXT},          // the round markers, from the right
    {16, 34, 6, 2, 208, 80, HUD_TEXT},          // (more of them)
    {12, 34, 4, 2, 224, 98, HUD_TEXT},          // (and more)
};
#define NPIECES (int)(sizeof pieces / sizeof pieces[0])
_Static_assert(NPIECES <= 16, "piece_dirty and tile_piece have a bit a piece");
static uint16_t piece_dirty;
static uint16_t tile_piece[0x80];       // the pieces each of the fixed rows' tiles (0x780-0x7ff) is in

// the tile RAM index of row R (0, 1, 34, 35), column C (0-27) of the turned screen
static inline int hud_index(int R, int C) { return mp_tile_index(R, 27 - C); }

static void layout_init(void) {
    memset(tile_piece, 0, sizeof tile_piece);
    for (int p = 0; p < NPIECES; p++)
        for (int r = pieces[p].r0; r < pieces[p].r0 + pieces[p].nr; r++)
            for (int c = pieces[p].c0; c < pieces[p].c0 + pieces[p].nc; c++) {
                int i = hud_index(r, c);
                if (i >= 0x780 && i < 0x800) tile_piece[i - 0x780] |= (uint16_t)(1u << p);
            }
    // the word CREDIT decides what the lives' and the credits' pieces show
    uint16_t word = 0;
    for (int p = 0; p < NPIECES; p++)
        if (pieces[p].kind == HUD_LIVES || pieces[p].kind == HUD_CREDIT) word |= (uint16_t)(1u << p);
    for (int c = 2; c < 8; c++) tile_piece[hud_index(35, c) - 0x780] |= word;
}

// a fixed row's tile as pens (colour * 4 + pixel): out[dy][dx]
static void hud_pens(int R, int C, uint8_t out[8][8]) {
    int i = hud_index(R, C);
    uint8_t attr = bd->vram[i + 0x800];
    const uint16_t *rows = mp_tile_rows[bd->vram[i]];
    int base = (attr & 0x3f) * 4;
    for (int dy = 0; dy < 8; dy++)
        for (int dx = 0; dx < 8; dx++) out[dy][dx] = (uint8_t)(base + ((rows[7 - dx] >> (2 * dy)) & 3));
}

// row 35 says CREDIT (the attract mode)
static bool credit_shown(void) {
    static const uint8_t word[6] = {'C', 'R', 'E', 'D', 'I', 'T'};
    for (int c = 0; c < 6; c++)
        if (bd->vram[hud_index(35, 2 + c)] != word[c]) return false;
    return true;
}

// the columns of a digit's tile made one in a score of 7 digits: 4 and 5.
// test/mp/hudcheck.c finds that any pair from 1-2 to 5-6 keeps every stroke
// and gap of Mappy's ten digits (0-1 and 6-7 don't)
#ifndef MPCV_SQUEEZE_AT
#define MPCV_SQUEEZE_AT 4
#endif

static void draw_piece(int p) {
    const piece_t *pc = &pieces[p];
    static uint8_t src[16 * 56], out[16 * 56];  // (the largest: 7 x 1 tiles, or 6 x 2)
    int w = pc->nc * 8, h = pc->nr * 8;
    bool blank0 = true;                 // the first tile has nothing in it
    bool space1 = true;                 // nor the second's column 0 (a digit's space)
    bool off = (pc->kind == HUD_LIVES && credit_shown()) || (pc->kind == HUD_CREDIT && !credit_shown());
    for (int r = 0; r < pc->nr; r++)
        for (int c = 0; c < pc->nc; c++) {
            uint8_t px[8][8];
            hud_pens(pc->r0 + r, pc->c0 + c, px);
            for (int dy = 0; dy < 8; dy++)
                for (int dx = 0; dx < 8; dx++) {
                    uint8_t v = off ? 0 : px[dy][dx];
                    if (c == 0 && (v & 3)) blank0 = false;
                    if (c == 1 && dx == 0 && (v & 3)) space1 = false;
                    src[(r * 8 + dy) * w + c * 8 + dx] = v;
                }
        }
    int ow = w, oh = h;
    if (pc->kind == HUD_SCORE && !blank0) {    // 7 tiles in 48: each loses a column
        ow = 48;
        const int j = MPCV_SQUEEZE_AT;
        for (int y = 0; y < h; y++) {
            const uint8_t *q = &src[y * w];
            uint8_t *o = &out[y * ow];
            for (int t = 0; t < 7; t++, q += 8)
                for (int x = 0; x < 8; x++) {
                    if (x == j + 1) continue;
                    if (x == j) *o++ = cvscale_pick(q[j], q[j + 1], q[j ? j - 1 : j], q[j + 2 < 8 ? j + 2 : j + 1]);
                    else *o++ = q[x];
                }
        }
    } else if (pc->kind == HUD_SCORE) { // its first tile (blank) left out, and the second's space moved to
        ow = w - 8;                     // the end, so the last digit doesn't touch the mansion
        const int skip = space1 ? 9 : 8;
        for (int y = 0; y < h; y++) {
            memcpy(&out[y * ow], &src[y * w + skip], (size_t)(w - skip));
            if (space1) out[y * ow + ow - 1] = src[y * w + 8];
        }
    } else {                            // as it is
        for (int y = 0; y < h; y++) memcpy(&out[y * ow], &src[y * w], (size_t)ow);
    }
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) margin[pc->y + y][MX(pc->x) + x] = mpcv_tile_tms[out[y * ow + x]];
    mark(pc->x, pc->y, pc->x + ow, pc->y + oh);
}

// ---- the sprites -----------------------------------------------------------------------
// A sprite of the board's, turned and scaled by itself: 16 x 16 to 12 x 12
// (3 of 4 each way, cvscale's pick), kept while its code, colour and flips
// stay the same
static uint32_t pic_stamp;              // the pictures made, for the cache's entries in use
static const simg_t *sprite_image(int code, int color, bool flipx, bool flipy) {
    uint32_t key = 1u << 16 | (uint32_t)(flipy ? 1 : 0) << 13 | (uint32_t)(flipx ? 1 : 0) << 12 | (uint32_t)color << 8 | (uint32_t)code;
    unsigned hs = (unsigned)(code ^ (color * 37) ^ (flipx ? 0x55 : 0) ^ (flipy ? 0x2a : 0));
    simg_t *s = NULL;
    for (unsigned k = 0; k < SCACHE; k++) {
        simg_t *t = &scache[(hs + k) & (SCACHE - 1)];
        if (t->key == key) { t->stamp = pic_stamp; return t; }
        if (!s && t->stamp != pic_stamp) s = t;
        if (!t->key || (s && k >= 8)) break;
    }
    if (!s) return NULL;                // (more sprites than the cache: never in the game)
    s->stamp = pic_stamp;
    // turned: picture column u is the sprite's line 15 - u, picture line v
    // its pixel v (mp_render's, turned as MAME turns it)
    uint8_t img[16][16];
    for (int u = 0; u < 16; u++) {
        uint64_t bits = mp_sprite_rows[code][flipy ? u : 15 - u];
        for (int v = 0; v < 16; v++) {
            int pen = (int)(bits >> (4 * (flipx ? 15 - v : v))) & 15;
            img[v][u] = mpcv_spr_tms[color * 16 + pen];
        }
    }
    uint8_t h[16][12];                  // the columns first
    for (int v = 0; v < 16; v++)
        for (int g = 0; g < 4; g++) {
            const uint8_t *p = &img[v][4 * g];
            h[v][3 * g] = p[0];
            h[v][3 * g + 1] = cvscale_pick(p[1], p[2], p[0], p[3]);
            h[v][3 * g + 2] = p[3];
        }
    for (int g = 0; g < 4; g++) {
        memcpy(s->px[3 * g], h[4 * g], 12);
        for (int x = 0; x < 12; x++)
            s->px[3 * g + 1][x] = cvscale_pick(h[4 * g + 1][x], h[4 * g + 2][x], h[4 * g][x], h[4 * g + 3][x]);
        memcpy(s->px[3 * g + 2], h[4 * g + 3], 12);
    }
    s->key = key;
    return s;
}

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The board's sprites as the picture's, in MAME's order (the last in
// front): each 16 x 16 of a sprite (one, two or four of them) scaled and
// put where the mansion's scaling puts the sprite's middle, 12 pixels
// from the next, clipped to the play area
static void find_sprites(void) {
    nvis = 0;
    const uint8_t *s1 = bd->spr[0], *s2 = bd->spr[1], *s3 = bd->spr[2];
    static const uint8_t gfx_offs[2][2] = {{0, 1}, {2, 3}};
    int off = 256 - bd->scroll;         // the mansion's column at the screen's X 0
    for (int offs = 0; offs < 0x80; offs += 2) {
        if (s3[offs + 1] & 2) continue;
        int sprite = s1[offs], color = s1[offs + 1] & 15;
        int sx = s2[offs + 1] + 0x100 * (s3[offs + 1] & 1) - 40;
        int sy = 256 - s2[offs] + 1;
        int flipx = s3[offs] & 1, flipy = (s3[offs] >> 1) & 1;
        int sizex = (s3[offs] >> 2) & 1, sizey = (s3[offs] >> 3) & 1;
        sprite &= ~sizex;
        sprite &= ~(sizey << 1);
        sy -= 16 * sizey;
        sy = (sy & 0xff) - 32;
        // turned: X = 223 - raw y, Y = raw x; the whole sprite's W x H, its middle
        int W = 16 * (sizey + 1), H = 16 * (sizex + 1);
        int X0 = 224 - sy - W, Y0 = sx;
        int U2 = 2 * (X0 + off) + W;            // its middle in the mansion's columns, doubled
        int y2 = 2 * (Y0 - 16) + H;             // and in its lines
        int cx = PF_X + fdiv(U2 * 5 + 7, 14) - cur_win, cy = fdiv(y2 * 3 + 4, 8);
        int ax = cx - 6 * (sizey + 1), ay = cy - 6 * (sizex + 1);
        for (int yy = 0; yy <= sizey; yy++)
            for (int xx = 0; xx <= sizex; xx++) {
                int code = (sprite + gfx_offs[yy ^ (sizey * flipy)][xx ^ (sizex * flipx)]) & 127;
                // this 16 x 16's place: raw (sx + 16 xx, sy + 16 yy), turned
                int px = ax + 12 * (sizey - yy), py = ay + 12 * xx;
                int x0 = px < PF_X ? PF_X : px, x1 = px + 12 > PF_X + PF_W ? PF_X + PF_W : px + 12;
                int y0 = py < 0 ? 0 : py, y1 = py + 12 > PF_H ? PF_H : py + 12;
                if (x0 >= x1 || y0 >= y1) continue;
                if (nvis == (int)(sizeof vis / sizeof vis[0])) return;
                const simg_t *img = sprite_image(code, color, flipx, flipy);
                if (!img) continue;
                vis_t *v = &vis[nvis++];
                v->x = (int16_t)px, v->y = (int16_t)py;
                v->x0 = (int16_t)x0, v->x1 = (int16_t)x1, v->y0 = (int16_t)y0, v->y1 = (int16_t)y1;
                v->img = img;
            }
    }
}

// ---- the two colours of 8 pixels -------------------------------------------------------
// 8 pixels (TMS9918 colours) as a pattern byte and a colour byte, the
// colour byte kept as the console has it where it will do (Time Pilot's)
static inline __attribute__((always_inline)) void span(mpcv_t *e, unsigned a, const uint8_t *c) {
    uint8_t vc = e->vram_col[a], vp = e->vram_pat[a];
    uint8_t c0 = c[0];
    if (c[1] == c0 && c[2] == c0 && c[3] == c0 && c[4] == c0 && c[5] == c0 && c[6] == c0 && c[7] == c0) {
        uint8_t vf = vc >> 4, vb = vc & 15;
        if (vf == c0 || vb == c0) {
            e->want_col[a] = vc;
            e->want_pat[a] = vf == vb ? vp : vf == c0 ? 0xFF : 0x00;
        } else {
            e->want_col[a] = (uint8_t)(c0 << 4 | c0);
            e->want_pat[a] = vp;
        }
        return;
    }
    {                                   // two colours
        uint8_t d = 0;
        unsigned m0 = 0;
        int i = 0;
        for (; i < 8; i++) {
            uint8_t x = c[i];
            if (x == c0) m0 |= 0x80u >> i;
            else if (d && x != d) break;
            else d = x;
        }
        if (i == 8) {
            static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
            int n0 = pop4[m0 & 15] + pop4[m0 >> 4];
            uint8_t a1 = n0 > 4 ? c0 : n0 < 4 ? d : (c0 < d ? c0 : d), a2 = a1 == c0 ? d : c0;
            uint8_t vf = vc >> 4, vb = vc & 15, fg, bg;
            if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
            else { fg = a2; bg = a1; }
            e->want_pat[a] = (uint8_t)(fg == c0 ? m0 : ~m0 & 0xFF);
            e->want_col[a] = (uint8_t)(fg << 4 | bg);
            return;
        }
    }
    uint8_t cnt[16] = {0};
    for (int i = 0; i < 8; i++) cnt[c[i]]++;
    // the two most used (ties: the lower colour)
    uint8_t a1 = 0, a2 = 0;
    for (int k = 1; k < 16; k++)
        if (cnt[k] > cnt[a1] || (!a1 && cnt[k])) a1 = (uint8_t)k;
    for (int k = 1; k < 16; k++)
        if (k != a1 && cnt[k] && (!a2 || cnt[k] > cnt[a2])) a2 = (uint8_t)k;
    uint8_t fg, bg, vf = vc >> 4, vb = vc & 15;
    if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
    else if (vf == a2 && vb == a1) { fg = a2; bg = a1; }
    else { fg = a2; bg = a1; }
    uint8_t pat = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t x = c[i];
        bool on = x == fg || (x != bg && NEARER(x, fg, bg));
        pat = (uint8_t)(pat << 1 | on);
    }
    e->want_pat[a] = pat;
    e->want_col[a] = (uint8_t)(fg << 4 | bg);
}

// ---- making the places -------------------------------------------------------------------
// the sprites over a place (8 x 8 at cell cx, band b), as TMS9918 colours
// (0: none), in o; false if none is there
MPCV_HOT static bool overlay(int b, int cx, uint8_t o[8][8]) {
    int px0 = cx * 8, py0 = b * 8;
    bool any = false;
    for (int k = 0; k < nvis; k++) {
        const vis_t *v = &vis[k];
        if (v->x0 >= px0 + 8 || v->x1 <= px0 || v->y0 >= py0 + 8 || v->y1 <= py0) continue;
        if (!any) memset(o, 0, 64), any = true;
        int x0 = v->x0 > px0 ? v->x0 : px0, x1 = v->x1 < px0 + 8 ? v->x1 : px0 + 8;
        int y0 = v->y0 > py0 ? v->y0 : py0, y1 = v->y1 < py0 + 8 ? v->y1 : py0 + 8;
        for (int y = y0; y < y1; y++) {
            const uint8_t *src = v->img->px[y - v->y];
            for (int x = x0; x < x1; x++) {
                uint8_t p = src[x - v->x];
                if (p) o[y - py0][x - px0] = p;
            }
        }
    }
    return any;
}

MPCV_HOT static void make_cell(mpcv_t *e, int b, int cx, const uint8_t (*o)[8]) {
    uint8_t c[8];
    e->bands |= 1u << b;
    bool play = cx >= PF_X / 8 && cx < (PF_X + PF_W) / 8;
    for (int k = 0; k < 8; k++) {
        int y = b * 8 + k;
        if (!play) {
            memcpy(c, &margin[y][MX(cx * 8)], 8);
        } else {
            const uint8_t *p = &wpen[y][cur_win + cx * 8 - PF_X];
            uint32_t w0, w1;
            memcpy(&w0, p, 4);
            memcpy(&w1, p + 4, 4);
            if (o && ((w0 | w1) & 0x80808080u))     // the sprites, but behind the tiles in front of them
                for (int i = 0; i < 8; i++) c[i] = (o[k][i] && !(p[i] & 0x80)) ? o[k][i] : p[i] & 0x7f;
            else {
                w0 &= 0x7f7f7f7fu;
                w1 &= 0x7f7f7f7fu;
                memcpy(c, &w0, 4);
                memcpy(c + 4, &w1, 4);
                if (o)
                    for (int i = 0; i < 8; i++)
                        if (o[k][i]) c[i] = o[k][i];
            }
        }
        span(e, (unsigned)(b * 256 + cx * 8 + k), c);
    }
    e->stats.cells += 8;
}

// The places to make again: those marked (tiles, pieces, the view moved)
// and those the sprites are in now or were in last time whose sprites
// differ (a hash of each place's overlay, kept)
MPCV_HOT static void make_places(mpcv_t *e) {
    uint32_t now[24];
    memset(now, 0, sizeof now);
    for (int k = 0; k < nvis; k++) {
        const vis_t *v = &vis[k];
        uint32_t m = (1u << ((v->x1 - 1) / 8 + 1)) - 1;
        if ((v->x1 - 1) / 8 >= 31) m = ~0u;
        m &= ~((1u << (v->x0 / 8)) - 1);
        for (int b = v->y0 / 8; b <= (v->y1 - 1) / 8; b++) now[b] |= m;
    }
    uint8_t o[8][8];
    for (int b = 0; b < 24; b++) {
        uint32_t m = now[b] | e->cells_prev[b] | cells[b];
        for (; m; m &= m - 1) {
            int cx = __builtin_ctz(m);
            bool over = (now[b] >> cx & 1) && overlay(b, cx, o);
            uint32_t h = 2166136261u;
            if (over)
                for (int k = 0; k < 8; k++) {
                    uint32_t w0, w1;
                    memcpy(&w0, &o[k][0], 4);
                    memcpy(&w1, &o[k][4], 4);
                    h = (h ^ w0) * 16777619u;
                    h = (h ^ w1) * 16777619u;
                }
            if (h != ov_hash[b][cx] || (cells[b] >> cx & 1)) {
                ov_hash[b][cx] = h;
                make_cell(e, b, cx, over ? (const uint8_t (*)[8])o : NULL);
            }
        }
        cells[b] = 0;
    }
    memcpy(e->cells_prev, now, sizeof now);
}

// ---- the picture -----------------------------------------------------------------------
void mpcv_board(mpcv_board_t *b) {
    memcpy(b->vram, mp_mem, sizeof b->vram);
    memcpy(b->spr[0], mp_mem + 0x1780, 0x80);
    memcpy(b->spr[1], mp_mem + 0x1f80, 0x80);
    memcpy(b->spr[2], mp_mem + 0x2780, 0x80);
    b->scroll = mp_scroll;
    b->frame = mp_frames;
}

// the scaled columns from lo to hi up to date: those not yet, scaled
static void keep_range(int lo, int hi) {
    if (range_lo > range_hi || hi < range_lo - 1 || lo > range_hi + 1) {
        range_lo = lo;
        range_hi = hi;
        rescale(lo, 0, hi, PF_H - 1);
        return;
    }
    if (lo < range_lo) {
        rescale(lo, 0, range_lo - 1, PF_H - 1);
        range_lo = lo;
    }
    if (hi > range_hi) {
        rescale(range_hi + 1, 0, hi, PF_H - 1);
        range_hi = hi;
    }
}

MPCV_HOT void mpcv_picture(mpcv_t *e, const mpcv_board_t *b) {
    bd = b;
    if (!colors_ready) colors_init();
    if (!pens_ready) pens_init();
    if (!maps_ready) maps_init();
    if (!pic_clear) {                   // (the plugin's room before the game)
        memset(&pic_u, 0, sizeof pic_u.p);
        memset(ov_hash, 0xff, sizeof ov_hash);
        range_lo = 1, range_hi = 0;
        pic_clear = true;
        e->pic_valid = false;
    }
    int win = window_of(b->scroll);
    cur_win = win;
    bool all = !e->pic_valid;
    if (all) {
        memset(margin, BLACK, sizeof margin);
        for (int i = 0; i < 24; i++) cells[i] = ~0u;
        piece_dirty = (uint16_t)((1u << NPIECES) - 1);
        memset(ov_hash, 0xff, sizeof ov_hash);
    }
    // the tiles that changed (all of them after a message): the mansion's,
    // and the fixed rows' (their pieces)
    static uint16_t changed[0x780];
    int nchanged = 0;
    for (int i = 0; i < 0x800; i += 4) {
        uint32_t c, a, sc, sa;
        memcpy(&c, b->vram + i, 4);
        memcpy(&a, b->vram + 0x800 + i, 4);
        memcpy(&sc, e->vram_seen + i, 4);
        memcpy(&sa, e->vram_seen + 0x800 + i, 4);
        if (!all && c == sc && a == sa) continue;
        for (int j = i; j < i + 4; j++) {
            if (!all && b->vram[j] == e->vram_seen[j] && b->vram[j + 0x800] == e->vram_seen[j + 0x800]) continue;
            if (j < 0x780) changed[nchanged++] = (uint16_t)j;
            else piece_dirty |= tile_piece[j - 0x780];
        }
    }
    memcpy(e->vram_seen, b->vram, sizeof e->vram_seen);
    // the mansion's tiles that changed, scaled again with their neighbours'
    // pixels (two made one); many of them: the view's columns all again,
    // and the rest of the mansion when it comes into view
    int lo = win - 2, hi = win + PF_W + 1;
    if (lo < 0) lo = 0;
    if (hi > WS - 1) hi = WS - 1;
    if (all || nchanged > 160) {
        range_lo = 1, range_hi = 0;
        e->stats.tiles += (uint32_t)nchanged;
    } else {
        for (int k = 0; k < nchanged; k++) {
            int i = changed[k], tc = 59 - (i >> 5), pr = i & 31;
            int u0 = tc * 8, y0 = pr * 8;
            int k0 = col_of[u0 ? u0 - 1 : 0], k1 = col_of[u0 + 8 < WU ? u0 + 8 : WU - 1];
            if (k0 < range_lo) k0 = range_lo;
            if (k1 > range_hi) k1 = range_hi;
            if (k0 > k1) continue;
            e->stats.tiles++;
            rescale(k0, row_of[y0 ? y0 - 1 : 0], k1, row_of[y0 + 8 < PL ? y0 + 8 : PL - 1]);
        }
    }
    keep_range(lo, hi);
    if (win != e->win) {
        e->win = (int16_t)win;
        mark_play();
        e->stats.scrolls++;
    }
    for (int p = 0; p < NPIECES; p++)
        if (piece_dirty >> p & 1) draw_piece(p);
    piece_dirty = 0;
    e->pic_valid = true;
    pic_stamp++;
    find_sprites();
    make_places(e);
    e->stats.pictures++;
    e->dirty = true;
}

void mpcv_canvas(mpcv_t *e, const uint8_t *px) {
    if (!colors_ready) colors_init();
    e->pic_valid = false;
    uint8_t c[8];
    for (int y = 0; y < MPCV_H; y++)
        for (int cx = 0; cx < 32; cx++) {
            for (int i = 0; i < 8; i++) {
                uint8_t v = px[y * MPCV_W + cx * 8 + i];
                c[i] = v ? v : BLACK;
            }
            span(e, (unsigned)((y >> 3) * 256 + cx * 8 + (y & 7)), c);
        }
    e->bands = ~0u;
    memset(e->cells_prev, 0xff, sizeof e->cells_prev);
    if (pic_clear) memset(ov_hash, 0xff, sizeof ov_hash);
    e->stats.pictures++;
    e->dirty = true;
}

void mpcv_screen(const mpcv_t *e, uint8_t *px) {
    for (int y = 0; y < MPCV_H; y++)
        for (int x = 0; x < MPCV_W; x++) {
            unsigned a = (unsigned)(((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7));
            uint8_t c = e->vram_col[a];
            c = (e->vram_pat[a] & (0x80 >> (x & 7))) ? c >> 4 : c & 15;
            px[y * MPCV_W + x] = c ? c : BLACK;
        }
}

void mpcv_ideal(const mpcv_t *e, uint8_t *px) {
    uint8_t o[8][8];
    for (int b = 0; b < 24; b++)
        for (int cx = 0; cx < 32; cx++) {
            bool over = overlay(b, cx, o);
            for (int k = 0; k < 8; k++)
                for (int i = 0; i < 8; i++) {
                    int y = b * 8 + k, x = cx * 8 + i;
                    bool play = x >= PF_X && x < PF_X + PF_W;
                    uint8_t w = play ? wpen[y][e->win + x - PF_X] : 0;
                    uint8_t c = !play ? margin[y][MX(x)] : w & 0x7f;
                    if (over && o[k][i] && !(play && (w & 0x80))) c = o[k][i];
                    px[y * MPCV_W + x] = c ? c : BLACK;
                }
        }
}

// ---- what the console needs ----------------------------------------------------------
static inline bool need_pat(const mpcv_t *e, unsigned j) { return e->want_pat[j] != e->vram_pat[j]; }
static inline bool need_col(const mpcv_t *e, unsigned j) { return e->want_col[j] != e->vram_col[j]; }

// The next bytes to send, from the cursor on (Q*bert's next_run)
MPCV_HOT static bool next_run(mpcv_t *e, bool *color, unsigned *s, int *m, unsigned most) {
    if (!e->dirty) return false;
    unsigned i = e->cursor;
    int band = -1;
    for (unsigned k = 0; k < MPCV_BYTES;) {
        if (!(i & 255)) {
            if (band >= 0) e->bands &= ~(1u << band);
            band = -1;
            if (k + 256 <= MPCV_BYTES && !(e->bands >> (i >> 8) & 1)) {
                k += 256;
                i = i + 256 == MPCV_BYTES ? 0 : i + 256;
                continue;
            }
            band = (int)(i >> 8);
        }
        if (!(i & 3) && k + 4 <= MPCV_BYTES) {
            uint32_t w, v, wc, vc;
            memcpy(&w, e->want_pat + i, 4);
            memcpy(&v, e->vram_pat + i, 4);
            memcpy(&wc, e->want_col + i, 4);
            memcpy(&vc, e->vram_col + i, 4);
            if (w == v && wc == vc) {
                k += 4;
                i = i + 4 == MPCV_BYTES ? 0 : i + 4;
                continue;
            }
        }
        bool nc = need_col(e, i);
        if (!nc && !need_pat(e, i)) {
            k++;
            i = i + 1 == MPCV_BYTES ? 0 : i + 1;
            continue;
        }
        e->cursor = (uint16_t)i;
        *color = nc;
        *s = i;
        unsigned end = i + 1, gap = 0;
        for (unsigned j = i + 1; j < MPCV_BYTES && j - i < most; j++) {
            if (nc ? need_col(e, j) : need_pat(e, j)) { end = j + 1; gap = 0; }
            else if (++gap > 3) break;
        }
        *m = (int)(end - i);
        return true;
    }
    e->dirty = false;
    return false;
}

static void put_run(mpcv_t *e, list_t *l, bool color, unsigned s, int n) {
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    uint8_t *d = l->b + l->n;
    if (color) {
        memcpy(d, e->want_col + s, n);
        memcpy(e->vram_col + s, d, n);
        e->stats.col_bytes += (unsigned)n;
    } else {
        memcpy(d, e->want_pat + s, n);
        memcpy(e->vram_pat + s, d, n);
        e->stats.pat_bytes += (unsigned)n;
    }
    l->n += n;
    e->stats.runs++;
}

// what's on screen written again as it is (Q*bert's)
static void refresh_run(mpcv_t *e, list_t *l, int n) {
    unsigned at = e->refresh_at;
    bool color = at >= MPCV_BYTES;
    unsigned s = color ? at - MPCV_BYTES : at;
    if (s + n > MPCV_BYTES) n = (int)(MPCV_BYTES - s);
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    memcpy(l->b + l->n, (color ? e->vram_col : e->vram_pat) + s, n);
    l->n += n;
    e->refresh_at = (uint16_t)((at + n) % (2 * MPCV_BYTES));
    e->stats.refreshed += (unsigned)n;
}

uint32_t mpcv_backlog(const mpcv_t *e) {
    uint32_t n = 0;
    for (unsigned j = 0; j < MPCV_BYTES; j++) n += need_pat(e, j) + need_col(e, j);
    return n;
}

// the name table: name i at place i in each third (it changes never)
static void names_run(mpcv_t *e, list_t *l, int n) {
    int left = 768 - e->names_at;
    if (n > left) n = left;
    l_run_head(l, (uint16_t)(NAMES + e->names_at), n);
    for (int k = 0; k < n; k++) l->b[l->n++] = (uint8_t)(e->names_at + k);
    e->names_at = (uint16_t)(e->names_at + n);
    if (e->names_at == 768) e->names_done = true;
}

// ---- a list, the console waiting for the vertical blank (messages) ----------------------
static void plain_list(mpcv_t *e, list_t *l) {
    if (e->resync) {
        l_end_vblank(l);
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
        if (e->resync-- == 2) return;
    } else if (e->tone_n) {
        l_psg(l, e->tone_bytes, e->tone_n);
    }
    while (!e->names_done) {
        int n = room(l);
        if (n < 16 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 1) break;
        names_run(e, l, n);
    }
    bool color;
    unsigned s;
    int m;
    while (e->names_done && next_run(e, &color, &s, &m, 255)) {
        int n = room(l);
        if (n < m && l->vblank) {
            if (n >= 8) put_run(e, l, color, s, n);
            l_end_vblank(l);
            continue;
        }
        if (n < 1) break;
        put_run(e, l, color, s, n < m ? n : m);
    }
    for (int k = 0; k < 2 && e->names_done; k++) {
        int n = room(l);
        if (n < 32) break;
        refresh_run(e, l, n);
    }
    l_end_vblank(l);
}

// ---- a list with samples in it (Q*bert's pcm_list) -------------------------------------
MPCV_HOT static void fill(mpcv_t *e, list_t *l, uint32_t until) {
    int avail = (int)until - (int)l->t;
    if (avail < C_RUN_ACT + C_BYTE_ACT) return;
    int n = (avail - C_RUN_ACT) / C_BYTE_ACT;
    if (n > 255) n = 255;
    if (!e->names_done) { names_run(e, l, n); return; }
    bool color;
    unsigned s;
    int m;
    if (!next_run(e, &color, &s, &m, 1)) {
        if (e->refresh_at % MPCV_BYTES + (unsigned)n > MPCV_BYTES) e->refresh_at = (uint16_t)(e->refresh_at / MPCV_BYTES * MPCV_BYTES);
        refresh_run(e, l, n);
        return;
    }
    if (s + (unsigned)n > MPCV_BYTES) s = MPCV_BYTES - (unsigned)n;
    put_run(e, l, color, s, n);
}

MPCV_HOT static void pcm_list(mpcv_t *e, list_t *l) {
    l->t = C_FRAME;
    l_end_vblank(l);
    unsigned tone_at = 0;
    for (int k = 0; k < MPCV_PCM_N; k++) {
        uint8_t b[10];
        int n = 0;
        for (int j = 0; j < 3; j++) b[n++] = e->pcm_bytes[k][j];
        while (tone_at < e->tone_n && n < 5) {
            unsigned g = 1;
            while (tone_at + g < e->tone_n && !(e->tone_bytes[tone_at + g] & 0x80)) g++;
            if (n + (int)g > 6) break;
            memcpy(b + n, e->tone_bytes + tone_at, g);
            n += (int)g;
            tone_at += g;
        }
        if (k + 1 == MPCV_PCM_N) {
            l_psg(l, b, n);
            break;
        }
        uint32_t until = PCM_FIRST + (((uint32_t)(k + 1) * PCM_STEP) >> 8);
        int avail = (int)until - (int)l->t - (C_PSG + C_PSG_BYTE * n);
        // a short run of the picture's as one of two (Q*bert's)
        bool color;
        unsigned s;
        int m = 0;
        bool two = e->names_done && next_run(e, &color, &s, &m, 7) && m <= 2 &&
                   avail >= 2 * C_RUN_ACT + C_BYTE_ACT * (m + 2);
        int need = two ? m + 1 : 1;
        int base = avail - (two ? 2 : 1) * C_RUN_ACT, best = 0, best_left = 99;
        for (int r = 0; r < 3 && base - C_PSG_BYTE * r >= C_BYTE_ACT * need; r++) {
            int left = (base - C_PSG_BYTE * r) % C_BYTE_ACT;
            if (left < best_left) { best_left = left; best = r; }
        }
        while (best--) b[n++] = e->pcm_bytes[k][2];     // (the last attenuator again: no change)
        l_psg(l, b, n);
        if (two) put_run(e, l, color, s, m);
        fill(e, l, until);
    }
    e->stats.pcm_frames++;
    e->resync = 2;
}

size_t mpcv_tv_frame(mpcv_t *e, uint8_t *out) {
    list_t l = {out, 0, C_START, true};
    if (e->pcm) pcm_list(e, &l);
    else plain_list(e, &l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->tone_n = 0;
    e->stats.tv_frames++;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    if (!e->dirty) e->stats.settled++;
    return l.n;
}

// ---- the sound: samples through the DAC ------------------------------------------------
// The DAC: the three tones at a period of 1, their attenuators as measured
// on a console (the kit's tools/ladder.json), summed. mpdac3.h's 512
// levels (tools/mkdac3.py) go from 0 to 2.40, each a triple of attenuators
// chosen so a small change makes small partial sums between a sample's
// three writes. The sound rests at the middle (DAC_MID). mpsnd.c hands
// over samples already scaled to the DAC's levels either side of the
// middle (+-32767 for +-DAC_HALF); a soft knee from 3/4 of the way rounds
// the loudest off, never clipping.
#define DAC_LEVELS MPDAC3_LEVELS
#define DAC_MID    255
#define DAC_HALF   255.0f
#define DAC_KNEE   191.0f
static const float ladder[3][16] = {    // tools/ladder.json's "amp", to 4 places
    {1.0f, 0.7914f, 0.6320f, 0.5084f, 0.4019f, 0.3161f, 0.2459f, 0.1912f, 0.1396f, 0.1080f, 0.0844f, 0.0672f, 0.0524f, 0.0421f, 0.0310f, 0.0f},
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
static uint8_t limit[512];                  // |level| (0-511) to 0-255: straight to DAC_KNEE, then soft
static int16_t lq[3][16];                   // ladder in 1/16384ths, for put_sample's integer sums
static bool dac_ready;

float mpcv_dac_level(int a0, int a1, int a2) { return ladder[0][a0 & 15] + ladder[1][a1 & 15] + ladder[2][a2 & 15]; }

static void dac_init(void) {
    for (int i = 0; i < 512; i++) {
        float v = (float)i;
        if (v > DAC_KNEE) v = DAC_KNEE + (DAC_HALF - DAC_KNEE) * (1.0f - expf(-(v - DAC_KNEE) / (DAC_HALF - DAC_KNEE)));
        limit[i] = (uint8_t)(v + 0.5f);
    }
    for (int c = 0; c < 3; c++)
        for (int a = 0; a < 16; a++) lq[c][a] = (int16_t)lrintf(ladder[c][a] * 16384.0f);
    dac_ready = true;
}

// a sample's three writes, in the order whose two partial sums (the chip
// between the writes, 52 T each) are nearest the new level, in integers
// (Donkey Kong's put_sample)
static const uint8_t order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static inline int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
static inline void put_sample(uint8_t *out, int *a0, int *a1, int *a2, uint16_t t) {
    const int w0 = t >> 8, w1 = t >> 4 & 15, w2 = t & 15;
    const int32_t d0 = lq[0][w0] - lq[0][*a0], d1 = lq[1][w1] - lq[1][*a1], d2 = lq[2][w2] - lq[2][*a2];
    const int32_t sum = d0 + d1 + d2;
    const int32_t f0 = iabs(sum - d0), f1 = iabs(sum - d1), f2 = iabs(sum - d2);
    const int32_t l0 = iabs(d0), l1 = iabs(d1), l2 = iabs(d2);
    int32_t best = f0 + l2, v;
    int pick = 0;
    if ((v = f0 + l1) < best) { best = v; pick = 1; }
    if ((v = f1 + l2) < best) { best = v; pick = 2; }
    if ((v = f1 + l0) < best) { best = v; pick = 3; }
    if ((v = f2 + l1) < best) { best = v; pick = 4; }
    if ((v = f2 + l0) < best) pick = 5;
    const uint32_t b = (uint32_t)(0x90 | w0) | (uint32_t)(0xB0 | w1) << 8 | (uint32_t)(0xD0 | w2) << 16;
    out[0] = (uint8_t)(b >> 8 * order[pick][0]);
    out[1] = (uint8_t)(b >> 8 * order[pick][1]);
    out[2] = (uint8_t)(b >> 8 * order[pick][2]);
    *a0 = w0; *a1 = w1; *a2 = w2;
}

MPCV_HOT void mpcv_sound_pcm(mpcv_t *e, const int16_t *s, int n) {
    if (!dac_ready) dac_init();
    e->tone_n = 0;
    if (n <= 0) {                       // silence: the whole chip, every list (a misread byte lasts a list)
        static const uint8_t off[] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
        memcpy(e->tone_bytes, off, sizeof off);
        e->tone_n = sizeof off;
        e->pcm = false;
        return;
    }
    if (!e->pcm) {
        e->pcm = true;
        e->pcm_gain = 0;
        e->dac_att[0] = e->dac_att[1] = e->dac_att[2] = 15;     // (as the lists before left them)
    }
    // Every list: the three tones parked at a period of 1, the noise silent,
    // so every register is written each list (a byte the console misreads
    // lasts a list at most)
    static const uint8_t park[] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00, 0xE4, 0xFF};
    memcpy(e->tone_bytes, park, sizeof park);
    e->tone_n = sizeof park;
    int32_t gain = e->pcm_gain;
    int a0 = e->dac_att[0], a1 = e->dac_att[1], a2 = e->dac_att[2];
    uint32_t clipped = 0;
    for (int k = 0; k < MPCV_PCM_N; k++) {
        int32_t v = s[k < n ? k : n - 1] >> 6;          // +-32767 to +-511 levels
        int32_t a = v < 0 ? -v : v;
        if (a > (int32_t)DAC_KNEE) clipped++;
        a = limit[a > 511 ? 511 : a];
        int32_t lvl = DAC_MID + (v < 0 ? -a : a);
        if (gain < 32 && !(k & 7)) gain++;              // (up to its rest over 43 ms)
        lvl = lvl * gain / 32;
        put_sample(e->pcm_bytes[k], &a0, &a1, &a2, mp_dac3[lvl < 0 ? 0 : lvl > DAC_LEVELS - 1 ? DAC_LEVELS - 1 : lvl]);
    }
    e->pcm_gain = (uint8_t)gain;
    e->dac_att[0] = (uint8_t)a0;
    e->dac_att[1] = (uint8_t)a1;
    e->dac_att[2] = (uint8_t)a2;
    e->stats.clipped += clipped;
}

void mpcv_prepare(void) {
    if (!colors_ready) colors_init();
    if (!pens_ready) pens_init();
    if (!maps_ready) maps_init();
}

void mpcv_init(mpcv_t *e) {
    memset(e, 0, sizeof *e);
    e->dirty = true;
    e->bands = (1u << (MPCV_BYTES / 256)) - 1;
    e->win = -1;
    if (!colors_ready) colors_init();
    if (!dac_ready) dac_init();
    layout_init();
    if (!maps_ready) maps_init();
    pens_ready = false;                 // (made at the first picture, from the board's PROMs)
    pic_clear = false;
}
