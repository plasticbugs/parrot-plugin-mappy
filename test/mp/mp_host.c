// mp_host.c -- the Mappy board model on the Mac, for the tests against MAME.
//
//   mp_host ROMDIR [-frames N] [-script FILE] [-dump FILE] [-pix FILE -pixevery N]
//           [-wav FILE] [-bot FROM -record FILE]
//           [-trace FILE] [-strace FILE] [-tracefrom F] [-traceto T]   (built with -DMP_TRACE)
//
// ROMDIR holds the set's files, unzipped (test/mp/setup.sh makes one under
// $TMPDIR). -dump writes the records mpdump.lua writes in MAME; -pix the
// pictures as MAME draws them; -wav the 15XX at 48 kHz (four of its 192 kHz
// ticks a sample); -bot plays from frame FROM on (test/mp/bot.h), and
// -record writes the script of what was pressed, for MAME to play the same;
// -trace and -strace the main and the sub CPU's instructions from VBLANK F
// to VBLANK T, as mpdump.lua has MAME's debugger write them.
//
// Script lines: "frame port hex", 1 for pressed, held from that frame on:
// coins (0x01 coin 1), p1 (0x02 right, 0x08 left), buttons (0x01 the
// button, 0x04 start 1, 0x08 start 2).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp.h"
#include "mpsnd.h"
#include "bot.h"

static FILE *trace[2];
static int trace_from = 2, trace_to = 1 << 30, cur_frame;
#ifdef MP_TRACE
static void on_trace(int cpu, uint64_t cycles) {
	if (!trace[cpu] || cur_frame <= trace_from || cur_frame > trace_to) return;
	uint16_t r[6];
	mp_regs(cpu, r);
	fprintf(trace[cpu], "@ %04X %04X %04X %04X %04X %04X %02X %llu\n", r[0], r[1], r[2], r[3], r[4], r[5], mp_regs8(cpu, 1),
	        (unsigned long long)cycles);
}
#endif

static uint8_t maincpu[0x6000], sub[0x2000], tiles[0x1000], spr0[0x2000], spr1[0x2000], proms[0x220], wave[0x100];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "mp_host: can't read %s\n", path); exit(1); }
	fclose(f);
}

static void wav_header(FILE *f, uint32_t rate, uint32_t n) {
	uint32_t bytes = n * 2;
	fwrite("RIFF", 1, 4, f);
	uint32_t v = 36 + bytes; fwrite(&v, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	v = 16; fwrite(&v, 4, 1, f);
	uint16_t h = 1; fwrite(&h, 2, 1, f);
	h = 1; fwrite(&h, 2, 1, f);
	fwrite(&rate, 4, 1, f);
	v = rate * 2; fwrite(&v, 4, 1, f);
	h = 2; fwrite(&h, 2, 1, f);
	h = 16; fwrite(&h, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&bytes, 4, 1, f);
}

// MAME's screen:pixels() in frame_done is the bitmap finished at the
// VBLANK before (the screen keeps two and has just swapped them)
static uint16_t fb[MP_W * MP_H], shown[MP_W * MP_H];
static int want_pix;
static void on_vblank(void) {
	if (!want_pix) return;
	memcpy(shown, fb, sizeof fb);
	mp_render(fb);
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: mp_host ROMDIR [options]\n"); return 2; }
	const char *dir = argv[1], *script = NULL, *dumpname = NULL, *pixname = NULL, *wavname = NULL;
	int frames = 600, pixevery = 0, botfrom = 0;
	FILE *record = NULL;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-frames")) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-script")) script = argv[++i];
		else if (!strcmp(argv[i], "-dump")) dumpname = argv[++i];
		else if (!strcmp(argv[i], "-pix")) pixname = argv[++i];
		else if (!strcmp(argv[i], "-pixevery")) pixevery = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
		else if (!strcmp(argv[i], "-trace")) trace[0] = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-strace")) trace[1] = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-tracefrom")) trace_from = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-traceto")) trace_to = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-bot")) botfrom = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-record")) record = fopen(argv[++i], "w");
		else { fprintf(stderr, "mp_host: what is %s?\n", argv[i]); return 2; }
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
	static struct { int frame; char port[8]; int v; } ev[16384];
	int nev = 0;
	if (script) {
		FILE *f = fopen(script, "r");
		if (!f) { fprintf(stderr, "mp_host: can't read %s\n", script); return 1; }
		char line[256];
		while (fgets(line, sizeof line, f) && nev < 16384)
			if (sscanf(line, "%d %7s %x", &ev[nev].frame, ev[nev].port, &ev[nev].v) == 3) nev++;
		fclose(f);
	}
	mpsnd_init(wave);
	mp_init(&roms);
#ifdef MP_TRACE
	mp_trace = on_trace;
#endif
	want_pix = pixname != NULL;
	mp_on_vblank = on_vblank;
	FILE *dump = dumpname ? fopen(dumpname, "wb") : NULL;
	FILE *pix = pixname ? fopen(pixname, "wb") : NULL;
	FILE *wav = wavname ? fopen(wavname, "wb") : NULL;
	uint32_t nsamples = 0;
	if (wav) wav_header(wav, 48000, 0);
	uint8_t held[3] = {0, 0, 0};        // coins, p1, buttons as pressed (1 pressed)
	for (int n = 1; n <= frames; n++) {
		cur_frame = n;
		mp_frame();
		if (pix && pixevery > 0 && n % pixevery == 0) {
			uint32_t fn = (uint32_t)n;
			uint16_t wh[2] = {MP_W, MP_H};
			fwrite(&fn, 4, 1, pix);
			fwrite(wh, 2, 2, pix);
			for (int i = 0; i < MP_W * MP_H; i++) {
				uint32_t c = mp_rgb[shown[i]];
				uint8_t px[4] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), 0xff};   // B G R A
				fwrite(px, 1, 4, pix);
			}
		}
		for (int i = 0; i < nev; i++) {
			if (ev[i].frame != n) continue;
			int k = !strcmp(ev[i].port, "coins") ? 0 : !strcmp(ev[i].port, "p1") ? 1 : !strcmp(ev[i].port, "buttons") ? 2 : -1;
			if (k < 0) continue;
			held[k] = (uint8_t)ev[i].v;
			if (record) fprintf(record, "%d %s %02X\n", n, ev[i].port, ev[i].v);
		}
		if (botfrom && n >= botfrom) {
			uint8_t p1, b;
			bot_play(n, &p1, &b);
			if (p1 != held[1]) { held[1] = p1; if (record) fprintf(record, "%d p1 %02X\n", n, p1); }
			if (b != held[2]) { held[2] = b; if (record) fprintf(record, "%d buttons %02X\n", n, b); }
		}
		mp_in.coins = (uint8_t)(0x0f & ~held[0]);
		mp_in.p1 = (uint8_t)(0x0f & ~held[1]);
		mp_in.buttons = (uint8_t)(0x0f & ~held[2]);
		if (dump) {
			static uint8_t rec[8 + 0x2800 + 0x400 + 0x20 + 2 * 14];
			uint8_t *p = rec;
			memcpy(p, "MPFR", 4); p += 4;
			uint32_t fn = (uint32_t)n; memcpy(p, &fn, 4); p += 4;
			memcpy(p, mp_mem, 0x2800); p += 0x2800;
			memcpy(p, mp_snd, 0x400); p += 0x400;
			for (int c = 0; c < 2; c++)
				for (int i = 0; i < 16; i++) *p++ = (uint8_t)(0xf0 | mp_io[c][i]);
			for (int c = 0; c < 2; c++) {
				uint16_t r[6];
				mp_regs(c, r);
				memcpy(p, r, 12); p += 12;
				*p++ = mp_regs8(c, 0);
				*p++ = mp_regs8(c, 1);
			}
			fwrite(rec, 1, (size_t)(p - rec), dump);
		}
		uint64_t now = mp_snd_now();
		while (mpsnd_tick() + 4 <= now) {
			double s = mpsnd_run(4) / 1024.0 / 4.0 * 32768.0;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			int16_t v = (int16_t)lrint(s);
			if (wav) fwrite(&v, 2, 1, wav);
			nsamples++;
		}
	}
	if (dump) fclose(dump);
	if (pix) fclose(pix);
	if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, 48000, nsamples); fclose(wav); }
	if (record) fclose(record);
	for (int c = 0; c < 2; c++) if (trace[c]) fclose(trace[c]);
	fprintf(stderr, "mp_host: %d frames, main %llu cycles, sub %llu, watchdog %u, sound dropped %u\n", frames,
	        (unsigned long long)mp_cycles(0), (unsigned long long)mp_cycles(1), mp_watchdog_fired, mpsnd_dropped);
	return 0;
}
