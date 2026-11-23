// textcheck.c -- the mansion's text, 6 x 6 (mpcv.c, "the mansion's text").
// Every text tile's letter (every letter, digit and mark the game's text
// uses) is made 6 x 6 from its 8 x 8 by making two pairs of its columns
// one, then two pairs of its lines, as cvscale_pick does; mpcv.c picks the
// pairs for each letter. This makes each letter again from the pairs
// mpcv.c says it used, checks it's what mpcv.c draws, and checks that
// every row (after the columns) and every column (after the lines) keeps
// its strokes and gaps: the same runs of pixels on and off, in the same
// order. It prints each letter, the board's beside the mansion's. The
// copyright sign fills its 8 x 8, so mpcv.c draws it instead; that's
// printed, not checked.
//
// Then the sprites of one or two pens (the points, the bonus round's
// numbers, GAME OVER), 16 x 16 to 12 x 12 by the pairs mpcv.c picked for
// each: it prints them, and which rows and columns lose a stroke or a gap.
// Those can't all keep everything (one pair in each 4 must go), so it
// counts them but doesn't fail on them; the points must keep everything.
//
//   cc -O1 -Isrc -Ilib -Isdk/firmware/revc/sdk -o textcheck test/mp/textcheck.c \
//      src/mp.c src/mpsnd.c src/mpcv.c sdk/firmware/revc/sdk/cvscale.c -lm
//   ./textcheck ROMDIR       (test/mp/setup.sh makes one)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"
#include "mp.h"
#include "mpcv.h"
#include "mpsnd.h"

static uint8_t maincpu[0x6000], sub[0x2000], tiles[0x1000], spr0[0x2000], spr1[0x2000], proms[0x220], wave[0x100];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "textcheck: can't read %s\n", path); exit(1); }
	fclose(f);
}

// a line's runs: its pens as runs, e.g. 0110 -> "010" (text: 0 paper, 1 ink)
static void runs(const uint8_t *px, int n, int step, char *out) {
	int k = 0;
	for (int x = 0; x < n; x++)
		if (!x || px[x * step] != px[(x - 1) * step]) out[k++] = (char)('0' + px[x * step]);
	out[k] = 0;
}

// n pixels (every step-th) with pairs made one (bit j of m: j and j + 1); returns how many
static int squeeze(const uint8_t *in, int n, int step, unsigned m, uint8_t *out, int ostep) {
	int k = 0;
	for (int x = 0; x < n; x++) {
		if (x && (m >> (x - 1) & 1)) continue;
		uint8_t v = in[x * step];
		if (m >> x & 1)
			v = cvscale_pick(v, in[(x + 1) * step], x ? in[(x - 1) * step] : 0, x + 2 < n ? in[(x + 2) * step] : 0);
		out[k++ * ostep] = v;
	}
	return k;
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: textcheck ROMDIR\n"); return 2; }
	const char *dir = argv[1];
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
	mpsnd_init(wave);
	mp_init(&roms);
	mpcv_prepare();
	int letters = 0, kept = 0, drawn = 0;
	for (int code = 0; code < 256; code++) {
		uint8_t rows[6], cols, lines;
		if (!mpcv_text_glyph(code, rows, &cols, &lines)) continue;
		letters++;
		uint8_t g[8][8], h[8][6], o[6][6];
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++) g[y][x] = (uint8_t)((mp_tile_rows[code][7 - x] >> (2 * y)) & 3);
		int changed = 0, differs = 0;
		if (cols || lines) {
			for (int y = 0; y < 8; y++) {
				squeeze(g[y], 8, 1, cols, h[y], 1);
				char a[12], b[12];
				runs(g[y], 8, 1, a);
				runs(h[y], 6, 1, b);
				changed += strcmp(a, b) != 0;
			}
			for (int x = 0; x < 6; x++) {
				squeeze(&h[0][x], 8, 6, lines, &o[0][x], 6);
				char a[12], b[12];
				runs(&h[0][x], 8, 6, a);
				runs(&o[0][x], 6, 6, b);
				changed += strcmp(a, b) != 0;
			}
			for (int y = 0; y < 6; y++)
				for (int x = 0; x < 6; x++) differs += (o[y][x] != 0) != ((rows[y] >> x & 1) != 0);
		}
		printf("tile 0x%02x: ", code);
		if (!cols && !lines) {
			drawn++;
			printf("drawn (it fills its 8 x 8)\n");
		} else {
			printf("columns %d-%d and %d-%d, lines %d-%d and %d-%d: ", __builtin_ctz(cols), __builtin_ctz(cols) + 1,
			       31 - __builtin_clz(cols), 32 - __builtin_clz(cols), __builtin_ctz(lines), __builtin_ctz(lines) + 1,
			       31 - __builtin_clz(lines), 32 - __builtin_clz(lines));
			if (differs) printf("NOT what mpcv.c draws (%d pixels)\n", differs);
			else if (changed) printf("%d rows or columns change\n", changed);
			else printf("every stroke and gap kept\n");
			if (!changed && !differs) kept++;
		}
		for (int y = 0; y < 8; y++) {
			printf("    ");
			for (int x = 0; x < 8; x++) putchar(g[y][x] ? '#' : '.');
			printf("   ");
			if (y < 6)
				for (int x = 0; x < 6; x++) putchar(rows[y] >> x & 1 ? '#' : '.');
			putchar('\n');
		}
	}
	printf("%d text tiles: %d keep every stroke and gap, %d drawn\n", letters, kept, drawn);
	int sprites = 0, skept = 0, points_bad = 0;
	for (int code = 0; code < 128; code++) {
		uint16_t cols, lines;
		uint8_t img[16][16], h[16][12], o[12][12];
		unsigned used = 0;
		for (int u = 0; u < 16; u++)
			for (int v = 0; v < 16; v++) {
				int pen = (int)(mp_sprite_rows[code][15 - u] >> (4 * v)) & 15;
				img[v][u] = (uint8_t)(pen == 15 ? 0 : pen + 1);
				if (pen != 15) used |= 1u << pen;
			}
		if (!used || __builtin_popcount(used) > 2 || !mpcv_spr_glyph(code, &cols, &lines)) continue;
		sprites++;
		int changed = 0;
		for (int v = 0; v < 16; v++) {
			squeeze(img[v], 16, 1, cols, h[v], 1);
			char a[20], b[20];
			runs(img[v], 16, 1, a);
			runs(h[v], 12, 1, b);
			changed += strcmp(a, b) != 0;
		}
		for (int x = 0; x < 12; x++) {
			squeeze(&h[0][x], 16, 12, lines, &o[0][x], 12);
			char a[20], b[20];
			runs(&h[0][x], 16, 12, a);
			runs(&o[0][x], 12, 12, b);
			changed += strcmp(a, b) != 0;
		}
		skept += !changed;
		if (changed && code >= 0x28 && code <= 0x31) points_bad++;
		printf("sprite 0x%02x: columns 0x%04x, lines 0x%04x: ", code, cols, lines);
		if (changed) printf("%d rows or columns change\n", changed);
		else printf("every stroke and gap kept\n");
		for (int v = 0; v < 16; v++) {
			printf("    ");
			for (int u = 0; u < 16; u++) putchar(img[v][u] ? '0' + img[v][u] - 1 : '.');
			printf("   ");
			if (v < 12)
				for (int x = 0; x < 12; x++) putchar(o[v][x] ? '0' + o[v][x] - 1 : '.');
			putchar('\n');
		}
	}
	printf("%d sprites of one or two pens: %d keep every stroke and gap%s\n", sprites, skept,
	       points_bad ? ", but the points DON'T" : ", the points among them");
	return kept + drawn == letters && letters && !points_bad ? 0 : 1;
}
