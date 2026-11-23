// hudcheck.c -- which pair of columns a 7-digit score merges (mpcv.c,
// MPCV_SQUEEZE_AT). A score of a million or more is 7 tiles, 56 pixels, in
// a 48-pixel margin, so each tile loses a column: columns j and j + 1
// become one, as cvscale_pick has it. This tries every j on the ten digits
// as the board draws them (turned, as MAME shows them) and prints the ones
// that keep every stroke and every gap of each line of each digit: the
// same runs of pixels on and off, in the same order.
//
//   cc -O1 -Isrc -Ilib -Isdk/firmware/revc/sdk -o hudcheck test/mp/hudcheck.c src/mp.c src/mpsnd.c -lm
//   ./hudcheck ROMDIR        (test/mp/setup.sh makes one)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"
#include "mp.h"
#include "mpsnd.h"

static uint8_t maincpu[0x6000], sub[0x2000], tiles[0x1000], spr0[0x2000], spr1[0x2000], proms[0x220], wave[0x100];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "hudcheck: can't read %s\n", path); exit(1); }
	fclose(f);
}

// a line's runs: the pixels on (1) and off (0) as runs, e.g. 0110 -> "010"
static int runs(const uint8_t *px, int n, char *out) {
	int k = 0;
	for (int x = 0; x < n; x++)
		if (!x || (px[x] != 0) != (px[x - 1] != 0)) out[k++] = px[x] ? '1' : '0';
	out[k] = 0;
	return k;
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: hudcheck ROMDIR\n"); return 2; }
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
	uint8_t g[10][8][8];                // the digits '0'-'9' (ASCII, as the board's text is), as MAME shows them
	for (int d = 0; d < 10; d++)
		for (int dy = 0; dy < 8; dy++)
			for (int dx = 0; dx < 8; dx++) g[d][dy][dx] = (uint8_t)((mp_tile_rows['0' + d][7 - dx] >> (2 * dy)) & 3);
	for (int dy = 0; dy < 8; dy++) {
		for (int d = 0; d < 10; d++) {
			for (int dx = 0; dx < 8; dx++) putchar(g[d][dy][dx] ? '#' : '.');
			putchar(' ');
		}
		putchar('\n');
	}
	int good = 0;
	for (int j = 0; j < 7; j++) {
		int lost = 0;
		for (int d = 0; d < 10; d++)
			for (int dy = 0; dy < 8; dy++) {
				const uint8_t *q = g[d][dy];
				uint8_t o[7];
				int n = 0;
				for (int x = 0; x < 8; x++) {
					if (x == j + 1) continue;
					o[n++] = x == j ? cvscale_pick(q[j], q[j + 1], q[j ? j - 1 : j], q[j + 2 < 8 ? j + 2 : j + 1]) : q[x];
				}
				char a[10], b[10];
				runs(q, 8, a);
				runs(o, 7, b);
				if (strcmp(a, b)) lost++;
			}
		printf("columns %d and %d: %s", j, j + 1, lost ? "" : "every stroke and gap kept");
		if (lost) printf("%d lines change", lost);
		printf("\n");
		if (!lost) good++;
	}
	return good ? 0 : 1;
}
