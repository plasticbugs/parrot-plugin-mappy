// cart_host.c -- the cart's side on the Mac: the board run a list at a time
// as the plugin runs it (src/mprun.c), the picture and the sound through
// src/mpcv.c. For previews, the lists' numbers and the lists themselves.
// (Time Pilot's.)
//
//   cart_host ROMDIR [-lists N] [-script FILE] [-bot FROM] [-png DIR -at L,L,...]
//             [-wav FILE] [-out LISTS.bin] [-from L] [-stats FILE] [-poke FRAME:ADDR:VALUE]
//
// -script: mp_host's script (frames, the bits pressed), each line's
// inputs set at that frame's VBLANK as mp_host and MAME set them. -bot:
// mp_host's bot from that frame. -png: at each list in -at, what the
// console shows (cv_LIST.png, 256 x 192 doubled), the picture before the
// TMS9918's two colours a place (ideal_LIST.png), MAME's (mame_LIST.png,
// 224 x 288) and MAME's and the console's side by side (pair_LIST.png).
// -wav: the DAC's samples, the attenuators the lists carry as levels (the
// measured ladder) at the lists' 5,962 Hz. -out: the lists from list -from
// on, as test/host/host.c records them (back to back), for
// tools/listlevel.py and tools/mame-replay.sh. -stats: a line a list (the
// list, its length, the picture's bytes in it, the pictures so far, whether
// the console still lacks some, the view's column).
// -poke: a byte of the main CPU's RAM set at a frame's VBLANK. All of
// these are made from the ROMs: keep them under $TMPDIR.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp.h"
#include "mpcv.h"
#include "mprun.h"
#include "mpsnd.h"
#include "bot.h"

static uint8_t maincpu[0x6000], sub[0x2000], tiles[0x1000], spr0[0x2000], spr1[0x2000], proms[0x220], wave[0x100];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "cart_host: can't read %s\n", path); exit(1); }
    fclose(f);
}

// ---- PNG, stored (no compression) ------------------------------------------------------
static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, size_t n) {
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    be32(h, crc(crc(0, (const uint8_t *)type, 4), d, n));
    fwrite(h, 1, 4, f);
}

static void png(const char *name, int w, int h, const uint8_t *rgb, int scale) {
    int W = w * scale, H = h * scale;
    size_t row = (size_t)W * 3 + 1, raw_n = row * H;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < W; x++) memcpy(raw + y * row + 1 + x * 3, rgb + ((size_t)(y / scale) * w + x / scale) * 3, 3);
    }
    size_t blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *p++ = off + n == raw_n;
        *p++ = (uint8_t)n; *p++ = (uint8_t)(n >> 8); *p++ = (uint8_t)~n; *p++ = (uint8_t)(~n >> 8);
        memcpy(p, raw + off, n);
        p += n;
    }
    be32(p, b << 16 | a);
    p += 4;
    FILE *f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "cart_host: can't write %s\n", name); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (size_t)(p - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void cv_png(const char *name, const uint8_t *px) {
    static uint8_t rgb[MPCV_W * MPCV_H * 3];
    for (int i = 0; i < MPCV_W * MPCV_H; i++) {
        uint32_t c = mpcv_tms_rgb[px[i] & 15];
        rgb[i * 3] = (uint8_t)(c >> 16); rgb[i * 3 + 1] = (uint8_t)(c >> 8); rgb[i * 3 + 2] = (uint8_t)c;
    }
    png(name, MPCV_W, MPCV_H, rgb, 2);
}

// ---- MAME's picture: drawn at each VBLANK, kept for the last few -----------------------
#define KEEP 4
static struct { uint32_t frame; uint16_t px[MP_W * MP_H]; } kept[KEEP];

// MAME's (turned, ROT90: 224 x 288) picture of frame f, as RGB
static bool mame_rgb(uint32_t f, uint8_t *rgb) {
    for (int k = 0; k < KEEP; k++) {
        if (kept[k].frame != f) continue;
        for (int Y = 0; Y < 288; Y++)
            for (int X = 0; X < 224; X++) {
                uint32_t c = mp_rgb[kept[k].px[(223 - X) * MP_W + Y]];
                uint8_t *o = rgb + (Y * 224 + X) * 3;
                o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
            }
        return true;
    }
    return false;
}

// MAME's picture (224 x 288) and the console's (256 x 192 at the TV's 4:3,
// so 288 high it is 384 wide), both doubled, on grey
static void pair_png(const char *name, const uint8_t *m, const uint8_t *px) {
    enum { MW = 448, MH = 576, CW = 768, PW = MW + 16 + CW, PH = MH };
    static uint8_t out[PW * PH * 3];
    memset(out, 0x30, sizeof out);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) memcpy(out + (y * PW + x) * 3, m + ((y / 2) * 224 + x / 2) * 3, 3);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < CW; x++) {
            uint32_t c = mpcv_tms_rgb[px[(y * MPCV_H / MH) * MPCV_W + x * MPCV_W / CW] & 15];
            uint8_t *o = out + (y * PW + MW + 16 + x) * 3;
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
        }
    png(name, PW, PH, out, 1);
}

// ---- the inputs at each VBLANK ----------------------------------------------------------
static struct { int frame; char port[8]; int v; } fev[16384];
static int nfev, botfrom;
static struct { int frame; uint16_t addr; uint8_t v; } pokes[16];
static int npokes;
static void (*mprun_vblank)(void);
static uint8_t held[3];                 // coins, p1, buttons as pressed (1 pressed)
static bool want_mame;

static void on_vblank(void) {
    uint32_t n = mp_frames;
    if (want_mame) {                    // MAME's picture of this VBLANK
        mp_render(kept[n % KEEP].px);
        kept[n % KEEP].frame = n;
    }
    mprun_vblank();
    // the inputs for the next frame, as mp_host sets them after its frame n
    for (int i = 0; i < nfev; i++) {
        if (fev[i].frame != (int)n) continue;
        int k = !strcmp(fev[i].port, "coins") ? 0 : !strcmp(fev[i].port, "p1") ? 1 : !strcmp(fev[i].port, "buttons") ? 2 : -1;
        if (k >= 0) held[k] = (uint8_t)fev[i].v;
    }
    if (botfrom && (int)n >= botfrom) bot_play((int)n, &held[1], &held[2]);
    mp_in.coins = (uint8_t)(0x0f & ~held[0]);
    mp_in.p1 = (uint8_t)(0x0f & ~held[1]);
    mp_in.buttons = (uint8_t)(0x0f & ~held[2]);
    for (int i = 0; i < npokes; i++)
        if (pokes[i].frame == (int)n && pokes[i].addr < 0x2800) mp_mem[pokes[i].addr] = pokes[i].v;
}

// ---- the lists' sound (the DAC's levels) ---------------------------------------------------
static int a0 = 15, a1 = 15, a2 = 15;
static int list_samples(const uint8_t *l, size_t n, int16_t *out) {
    int k = 0;
    size_t i = 0;
    bool vblank = true;
    while (i < n) {
        uint8_t c = l[i];
        if (c == 1) i += 4 + l[i + 3];
        else if (c == 2) {
            int m = l[i + 1];
            bool got = false;
            for (int j = 0; j < m; j++) {
                uint8_t b = l[i + 2 + j];
                if ((b & 0xF0) == 0x90) { a0 = b & 15; got = true; }
                if ((b & 0xF0) == 0xB0) { a1 = b & 15; got = true; }
                if ((b & 0xF0) == 0xD0) { a2 = b & 15; got = true; }
            }
            if (got && k < MPCV_PCM_N) out[k++] = (int16_t)((mpcv_dac_level(a0, a1, a2) - 1.20f) * 12500.0f);
            i += 2 + (size_t)m;
        } else if (c == 3 && vblank) i += 3;
        else if (c == 0 && vblank) { vblank = false; i++; }
        else break;
    }
    return k;
}

static void wav_header(FILE *f, uint32_t rate, uint32_t n) {
    uint32_t v;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
}

static bool listed(const char *at, int l) {
    char key[16];
    snprintf(key, sizeof key, "%d", l);
    const char *s = at;
    while (*s) {
        const char *e = strchr(s, ',');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == strlen(key) && !strncmp(s, key, len)) return true;
        s = e ? e + 1 : s + len;
    }
    return false;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cart_host ROMDIR [options]\n"); return 2; }
    const char *dir = argv[1], *script = NULL, *pngdir = NULL, *at = "", *wavname = NULL, *statsname = NULL,
               *outname = NULL;
    int lists = 2400, from = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-lists")) lists = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-script")) script = argv[++i];
        else if (!strcmp(argv[i], "-bot")) botfrom = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-png")) pngdir = argv[++i];
        else if (!strcmp(argv[i], "-at")) at = argv[++i];
        else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
        else if (!strcmp(argv[i], "-stats")) statsname = argv[++i];
        else if (!strcmp(argv[i], "-out")) outname = argv[++i];
        else if (!strcmp(argv[i], "-from")) from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-poke") && npokes < 16) {
            unsigned f, a, v;
            if (sscanf(argv[++i], "%u:%x:%x", &f, &a, &v) != 3) { fprintf(stderr, "cart_host: -poke FRAME:ADDR:VALUE\n"); return 2; }
            pokes[npokes].frame = (int)f; pokes[npokes].addr = (uint16_t)a; pokes[npokes].v = (uint8_t)v; npokes++;
        } else { fprintf(stderr, "cart_host: what is %s?\n", argv[i]); return 2; }
    }
    load(dir, "mpx_3.1d", maincpu, 0x2000);
    load(dir, "mp1_2.1c", maincpu + 0x2000, 0x2000);
    load(dir, "mpx_1.1b", maincpu + 0x4000, 0x2000);
    load(dir, "mp1_4.1k", sub, 0x2000);
    load(dir, "mp1_5.3b", tiles, 0x1000);
    load(dir, "mp1_6.3m", spr0, 0x2000);
    load(dir, "mp1_7.3n", spr1, 0x2000);
    load(dir, "mp1-5.5b", proms, 0x20);
    load(dir, "mp1-6.4c", proms + 0x20, 0x100);
    load(dir, "mp1-7.5k", proms + 0x120, 0x100);
    load(dir, "mp1-3.3m", wave, 0x100);
    mp_roms_t roms = {maincpu, sub, tiles, spr0, spr1, proms, wave};
    if (script) {
        FILE *f = fopen(script, "r");
        if (!f) { fprintf(stderr, "cart_host: can't read %s\n", script); return 1; }
        char line[256];
        while (fgets(line, sizeof line, f) && nfev < 16384)
            if (sscanf(line, "%d %7s %x", &fev[nfev].frame, fev[nfev].port, &fev[nfev].v) == 3) nfev++;
        fclose(f);
    }
    static mpcv_t cv;
    static mprun_t run;
    mpcv_init(&cv);
    mpsnd_init(wave);
    mp_init(&roms);
    mpcv_prepare();
    mprun_start(&run, &cv);
    mprun_vblank = mp_on_vblank;
    mp_on_vblank = on_vblank;
    want_mame = pngdir && *at;

    FILE *wav = wavname ? fopen(wavname, "wb") : NULL;
    if (wav) wav_header(wav, (uint32_t)(MPCV_RATE_Q8 >> 8), 0);
    uint32_t nsamples = 0;
    FILE *stats = statsname ? fopen(statsname, "w") : NULL;
    FILE *out = outname ? fopen(outname, "wb") : NULL;
    static uint8_t list[MPCV_LIST_MAX], px[MPCV_W * MPCV_H];
    uint64_t bytes_sum = 0;
    uint32_t len_max = 0, pics0 = 0, sec_pics = 0, worst_sec = 1000, best_sec = 0, secs = 0;
    for (int l = 1; l <= lists; l++) {
        uint32_t t0 = cv.stats.pat_bytes + cv.stats.col_bytes;
        size_t n = mprun_list(&run, list);
        uint32_t t1 = cv.stats.pat_bytes + cv.stats.col_bytes;
        bytes_sum += t1 - t0;
        if (n > len_max) len_max = (uint32_t)n;
        if (wav) {
            int16_t s[MPCV_PCM_N];
            int k = list_samples(list, n, s);
            fwrite(s, 2, (size_t)k, wav);
            nsamples += (uint32_t)k;
        }
        if (out && l >= from) {
            const uint8_t h[9] = {'F', 0, 0, (uint8_t)n, (uint8_t)(n >> 8), 0, 0, 0, cv.pcm ? 1 : 0};
            fwrite(h, 1, 9, out);
            fwrite(list, 1, n, out);
        }
        if (stats) fprintf(stats, "%d %zu %u %u %u %d\n", l, n, t1 - t0, run.pictures, cv.dirty ? 1 : 0, cv.win);
        if (l % 60 == 0) {                  // pictures a second: each 60 lists (about a second)
            uint32_t p = run.pictures - pics0;
            pics0 = run.pictures;
            if (l > 120) {
                sec_pics += p;
                secs++;
                if (p < worst_sec) worst_sec = p;
                if (p > best_sec) best_sec = p;
            }
        }
        if (pngdir && listed(at, l)) {
            char name[1024];
            mpcv_screen(&cv, px);
            snprintf(name, sizeof name, "%s/cv_%d.png", pngdir, l);
            cv_png(name, px);
            mpcv_ideal(&cv, px);
            snprintf(name, sizeof name, "%s/ideal_%d.png", pngdir, l);
            cv_png(name, px);
            static uint8_t m[224 * 288 * 3];
            mpcv_screen(&cv, px);
            if (run.shown && mame_rgb(run.shown->frame, m)) {
                snprintf(name, sizeof name, "%s/mame_%d.png", pngdir, l);
                png(name, 224, 288, m, 1);
                snprintf(name, sizeof name, "%s/pair_%d.png", pngdir, l);
                pair_png(name, m, px);
            }
            printf("list %d: frame %u, scroll %u, window %d, backlog %u\n", l, run.shown ? run.shown->frame : 0,
                   run.shown ? run.shown->scroll : 0, cv.win, mpcv_backlog(&cv));
        }
    }
    if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, (uint32_t)(MPCV_RATE_Q8 >> 8), nsamples); fclose(wav); }
    if (stats) fclose(stats);
    if (out) fclose(out);
    const mpcv_stats_t *st = &cv.stats;
    printf("cart_host: %d lists, %u pictures (%.1f a second; seconds %u to %u), waited %u\n", lists, run.pictures,
           secs ? (double)sec_pics / secs * 59.62 / 60.0 : 0.0, worst_sec, best_sec, run.waited);
    printf("  bytes %llu (pattern %u, colour %u, refreshed %u), %.0f a list, %.0f a picture; runs %u; longest list %u bytes, %u T\n",
           (unsigned long long)bytes_sum, st->pat_bytes, st->col_bytes, st->refreshed, (double)bytes_sum / lists,
           run.pictures ? (double)bytes_sum / run.pictures : 0.0, st->runs, len_max, st->t_max);
    printf("  settled %u lists, tiles %u, cells %u, view moved %u, clipped samples %u, sound dropped %u\n", st->settled,
           st->tiles, st->cells / 8, st->scrolls, st->clipped, mpsnd_dropped);
    return 0;
}
