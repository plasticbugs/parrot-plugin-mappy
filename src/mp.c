// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles
//
// mp.c -- see mp.h. The Mappy board as MAME 0.288 has it:
//
//   namco/mappy.cpp     the memory maps, the LS259 latch's outputs, the
//                       VBLANK interrupt and the 58XXs' 50 us run timers,
//                       the screen's raw timing, the maximum quantum
//   namco/mappy_v.cpp   the palette, the tilemap, the sprites
//   machine/namcoio.cpp the 58XX custom I/O chip
//   machine/74259.cpp   the latch (every output falls to 0 at reset)
//   emu/screen.cpp      the VBLANK, VBLANK end and scanline 0 timers
//   emu/sound.cpp       the sound manager's 50 Hz update timer
//
// by Nicola Salmoria, Aaron Giles and the MAME team, BSD-3-Clause. The
// CPUs are the kit's lib/m6809 and the scheduler its lib/sched (MAME's,
// in C), so both 6809s run the same cycles, in the same slices, as MAME's.
#include "mp.h"

#include <string.h>

#include "sched/sched.h"
#include "m6809/m6809.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define MP_HOT __attribute__((section(".time_critical.mp")))
#else
#define MP_HOT
#endif

mp_inputs_t mp_in = {0x0f, 0x0f, 0x0f, 0x0f, MP_DSW0, MP_DSW1, MP_DSW2};

uint8_t mp_mem[0x2800];
uint8_t mp_snd[0x400];
uint8_t mp_io[2][16];
uint8_t mp_scroll;
uint8_t mp_latch;
uint32_t mp_frames;
uint32_t mp_watchdog_fired;
uint32_t mp_tile_dirty[0x800 / 32];
void (*mp_on_vblank)(void);

uint32_t mp_rgb[512];
uint32_t mp_pal[32];
uint8_t mp_tile_ctab[256];
uint8_t mp_spr_ctab[256];
uint16_t mp_tile_rows[256][8];
uint64_t mp_sprite_rows[128][16];

static const uint8_t *rom_main;          // 0xa000-0xffff
static const uint8_t *rom_sub;           // 0xe000-0xffff
static m6809_t cpu_main, cpu_sub;
static int main_irq_mask, sub_irq_mask;
static int watchdog;                     // VBLANKs left

// ---- the 58XXs (namcoio.cpp) -------------------------------------------------------------
typedef struct {
	int reset;
	int lastcoins, lastbuttons;
	int credits;
	int coins[2], coins_per_cred[2], creds_per_coin[2];
} io_t;
static io_t io[2];
static int dipmux_select;                // the LS157 (the 58XX #1's out0, bit 0)

static int io_in(int chip, int n) {
	if (chip == 0) {
		switch (n) {
		case 0: return mp_in.coins;
		case 1: return mp_in.p1;
		case 2: return mp_in.p2;
		default: return mp_in.buttons;
		}
	}
	switch (n) {
	case 0: return (dipmux_select ? mp_in.dsw2 >> 4 : mp_in.dsw2) & 0x0f;
	case 1: return mp_in.dsw1;
	case 2: return mp_in.dsw1 >> 4;
	default: return mp_in.dsw0;
	}
}
static void io_out0(int chip, int v) {
	if (chip == 1) dipmux_select = v & 1;
}

static void io_set_reset_line(int chip, int assert) {
	io_t *c = &io[chip];
	c->reset = assert;
	if (assert) {
		c->credits = 0;
		c->coins[0] = 0;
		c->coins_per_cred[0] = 1;
		c->creds_per_coin[0] = 1;
		c->coins[1] = 0;
		c->coins_per_cred[1] = 1;
		c->creds_per_coin[1] = 1;
	}
}

#define IORAM(o) (ram[o] & 0x0f)
#define IOSET(o, v) (ram[o] = (uint8_t)((v) & 0x0f))

static void io_handle_coins(int chip, int swap) {
	io_t *c = &io[chip];
	uint8_t *ram = mp_io[chip];
	int credit_add = 0, credit_sub = 0;
	int val = ~io_in(chip, 0);
	int toggled = val ^ c->lastcoins;
	c->lastcoins = val;
	for (int k = 0; k < 2; k++) {
		if (val & toggled & (1 << k)) {
			c->coins[k]++;
			if (c->coins[k] >= (c->coins_per_cred[k] & 7)) {
				credit_add = c->creds_per_coin[k] - (c->coins_per_cred[k] >> 3);
				c->coins[k] -= c->coins_per_cred[k] & 7;
			} else if (c->coins_per_cred[k] & 8)
				credit_add = 1;
		}
	}
	if (val & toggled & 0x08) credit_add = 1;

	val = ~io_in(chip, 3);
	toggled = val ^ c->lastbuttons;
	c->lastbuttons = val;
	if (IORAM(9) == 0) {
		if (val & toggled & 0x04) {
			if (c->credits >= 1) credit_sub = 1;
		} else if (val & toggled & 0x08) {
			if (c->credits >= 2) credit_sub = 2;
		}
	}
	c->credits += credit_add - credit_sub;
	IOSET(0 ^ swap, c->credits / 10);
	IOSET(1 ^ swap, c->credits % 10);
	if (credit_add) IOSET(2 ^ swap, credit_add);
	if (credit_sub) IOSET(3 ^ swap, credit_sub);
	IOSET(4, ~io_in(chip, 1));
	IOSET(5, ((val & 0x05) << 1) | (val & toggled & 0x05));
	IOSET(6, ~io_in(chip, 2));
	IOSET(7, (val & 0x0a) | ((val & toggled & 0x0a) >> 1));
}

#define NEXT(n) ((((n) & 1) ? (n) ^ 0x90 : (n)) >> 1)

static void io_run(int chip) {          // namco58xx_device::customio_run
	uint8_t *ram = mp_io[chip];
	switch (IORAM(8)) {
	case 1:
		IOSET(4, ~io_in(chip, 0));
		IOSET(5, ~io_in(chip, 1));
		IOSET(6, ~io_in(chip, 2));
		IOSET(7, ~io_in(chip, 3));
		io_out0(chip, IORAM(9));
		break;
	case 2:
		io[chip].coins_per_cred[0] = IORAM(9);
		io[chip].creds_per_coin[0] = IORAM(10);
		io[chip].coins_per_cred[1] = IORAM(11);
		io[chip].creds_per_coin[1] = IORAM(12);
		break;
	case 3:
		io_handle_coins(chip, 2);
		break;
	case 4:
		io_out0(chip, 0);
		IOSET(0, ~io_in(chip, 0));
		IOSET(2, ~io_in(chip, 1));
		IOSET(4, ~io_in(chip, 2));
		IOSET(6, ~io_in(chip, 3));
		io_out0(chip, 1);
		IOSET(1, ~io_in(chip, 0));
		IOSET(3, ~io_in(chip, 1));
		IOSET(5, ~io_in(chip, 2));
		IOSET(7, ~io_in(chip, 3));
		break;
	case 5: {
		int n = (IORAM(9) * 16 + IORAM(10)) & 0x7f;
		int seed = 0x22;
		for (int i = 0; i < n; i++) seed = NEXT(seed);
		for (int i = 1; i < 8; i++) {
			int rng = seed;
			n = 0;
			if (rng & 1) n ^= ~IORAM(11);
			rng = NEXT(rng);
			seed = rng;
			if (rng & 1) n ^= ~IORAM(10);
			rng = NEXT(rng);
			if (rng & 1) n ^= ~IORAM(9);
			rng = NEXT(rng);
			if (rng & 1) n ^= ~IORAM(15);
			rng = NEXT(rng);
			if (rng & 1) n ^= ~IORAM(14);
			rng = NEXT(rng);
			if (rng & 1) n ^= ~IORAM(13);
			rng = NEXT(rng);
			if (rng & 1) n ^= ~IORAM(12);
			IOSET(i, ~n);
		}
		IOSET(0, 0);
		if (IORAM(9) == 0xf) IOSET(0, 0xf);
		break;
	}
	default:
		break;
	}
}

// ---- the scheduler -----------------------------------------------------------------------
enum { D_MAIN, D_SUB, D_N };                 // MAME's device order
enum { L_MAIN_IRQ, L_SUB_IRQ, L_SUB_RESET };

static void latch_w(int offset);
static void snd_reg_w(int reg, uint8_t v);

static inline void tile_dirty(unsigned a) {
	a &= 0x7ff;
	mp_tile_dirty[a >> 5] |= 1u << (a & 31);
}

static inline uint8_t main_rd(uint16_t a) {
	if (a >= 0xa000) return rom_main[a - 0xa000];
	if (a < 0x2800) return mp_mem[a];
	if ((a & 0xfc00) == 0x4000) return mp_snd[a & 0x3ff];
	if ((a & 0xffe0) == 0x4800) return (uint8_t)(0xf0 | mp_io[(a >> 4) & 1][a & 15]);
	return 0;
}
static inline void main_wr(uint16_t a, uint8_t v) {
	if (a < 0x2800) {
		if (a < 0x1000 && mp_mem[a] != v) tile_dirty(a);
		mp_mem[a] = v;
	} else if (a >= 0x3800 && a < 0x4000) {
		mp_scroll = (uint8_t)((a - 0x3800) >> 3);
	} else if ((a & 0xfc00) == 0x4000) {
		if (a < 0x4040) snd_reg_w(a & 0x3f, v);
		else mp_snd[a & 0x3ff] = v;
	} else if ((a & 0xffe0) == 0x4800) {
		mp_io[(a >> 4) & 1][a & 15] = v & 0x0f;
	} else if ((a & 0xfff0) == 0x5000) {
		latch_w(a & 15);
	} else if (a == 0x8000) {
		watchdog = 8;
	}
}
static inline uint8_t sub_rd(uint16_t a) {
	if (a >= 0xe000) return rom_sub[a - 0xe000];
	if (a < 0x400) return mp_snd[a];
	return 0;
}
static inline void sub_wr(uint16_t a, uint8_t v) {
	if (a < 0x400) {
		if (a < 0x40) snd_reg_w(a, v);
		else mp_snd[a] = v;
	} else if ((a & 0xfff0) == 0x2000) {
		latch_w(a & 15);
	}
}

#ifdef MP_TRACE
void (*mp_trace)(int cpu, uint64_t cycles);
uint64_t sched_total_cycles(int d);
#define M6809_TRACE(c) do { if (mp_trace) mp_trace(D_MAIN, sched_total_cycles(D_MAIN)); } while (0)
#endif
#define M6809_NAME(x) mainc_##x
#define M6809_RD(a) main_rd(a)
#define M6809_WR(a, v) main_wr(a, v)
#define M6809_HOT MP_HOT
#include "m6809/m6809.c"
#undef M6809_NAME
#undef M6809_RD
#undef M6809_WR

#ifdef MP_TRACE
#undef M6809_TRACE
#define M6809_TRACE(c) do { if (mp_trace) mp_trace(D_SUB, sched_total_cycles(D_SUB)); } while (0)
#endif
#define M6809_NAME(x) subc_##x
#define M6809_RD(a) sub_rd(a)
#define M6809_WR(a, v) sub_wr(a, v)
#define M6809_HOT MP_HOT
#include "m6809/m6809.c"
#undef M6809_NAME
#undef M6809_RD
#undef M6809_WR

// MAME's 6809 fetches the reset vector in the CPU's first timeslice after
// a reset (the state machine's INTERRUPT_VECTOR: 4 cycles of icount);
// lib/m6809's reset counts those 4 cycles outside any run. They are taken
// from the next runs here, so both CPUs keep MAME's time.
static int debt[D_N];                   // the vector's cycles still to take
static int slice_debt[D_N];             // the ones the running slice took
static inline int cpu_run(int d, int n) {
	int k = debt[d];
	slice_debt[d] = 0;
	if (__builtin_expect(k != 0, 0)) {
		if (n <= k) {
			debt[d] = k - n;
			return n;
		}
		debt[d] = 0;
		slice_debt[d] = k;
		n -= k;
	}
	return (d == D_MAIN ? mainc_run(&cpu_main, n) : subc_run(&cpu_sub, n)) + k;
}
static void cpu_reset(int d) {
	if (d == D_MAIN) mainc_reset(&cpu_main);
	else subc_reset(&cpu_sub);
	debt[d] = 4;
}
#define SCHED_CPUS D_N
#define SCHED_RUN(d, n) cpu_run((d), (n))
#define SCHED_ABORT(d) m6809_abort((d) == D_MAIN ? &cpu_main : &cpu_sub)
#define SCHED_DONE(d) (m6809_done((d) == D_MAIN ? &cpu_main : &cpu_sub) + slice_debt[d])
#define SCHED_RESET(d) cpu_reset(d)
#define SCHED_TIMERS 24
#include "sched/sched.c"

// the screen: set_raw(6.144 MHz, 384, 0, 288, 264, 0, 224), as screen.cpp works it out
#define PIXELTIME ((sched_time_t)(SCHED_SEC / 6144000))         // 162760416666
#define SCANTIME (PIXELTIME * 384)
#define FRAME_PERIOD (SCANTIME * 264)
#define VBLANK_PERIOD (SCANTIME * (264 - 224))

static int tm_snd50, tm_vbstart, tm_vbend, tm_scan0, tm_io[2];

static void main_irq_set(int state) { m6809_set_irq(&cpu_main, state); }
static void sub_irq_set(int state) { m6809_set_irq(&cpu_sub, state); }

#define CYCLE ((sched_time_t)(SCHED_SEC / MP_CLOCK))     // attoseconds_per_cycle

// MAME's 6809 counts each bus cycle as it makes it, so a handler's time is
// the write's own cycle's end. lib/m6809 charges an instruction when it
// ends, and sched_now() in a handler is the instruction's start; an STA
// or STB says which of its cycles wrote (wofs). The handlers that need the
// time (the latch, the sound) add it. (Every latch write in the ROMs is an
// STA; the 15XX's writes are other instructions too, where it is near.)
static inline sched_time_t write_delay(void) {
	int d = sched_executing();
	if (d < 0) return 0;
	return (sched_time_t)((d == D_MAIN ? cpu_main.wofs : cpu_sub.wofs) + 1) * CYCLE;
}

uint64_t mp_snd_now(void) { return sched_ticks(sched_now() + write_delay(), MP_SND_RATE); }

static void snd_reg_w(int reg, uint8_t v) {
	if (mp_snd[reg] == v) return;
	mp_snd[reg] = v;
	mp_snd_write(mp_snd_now(), reg, v);
}

static void latch_out(int bit, int d) {
	switch (bit) {
	case 0:
		sub_irq_mask = d;
		if (!d) sched_set_input(L_SUB_IRQ, SCHED_CLEAR_LINE);
		break;
	case 1:
		main_irq_mask = d;
		if (!d) sched_set_input(L_MAIN_IRQ, SCHED_CLEAR_LINE);
		break;
	case 3:
		mp_snd_enable(mp_snd_now(), d);
		break;
	case 4:
		io_set_reset_line(0, !d);
		io_set_reset_line(1, !d);
		break;
	case 5:
		sched_set_input(L_SUB_RESET, d ? SCHED_CLEAR_LINE : SCHED_ASSERT_LINE);
		break;
	default:                             // 2: flip screen; 6, 7: not connected
		break;
	}
}

// The latch's outputs act at the write's own time: a one-shot timer then.
// MAME's CPU makes the write, and its set_input_line synchronizes, at that
// cycle (if it's past the timeslice's end, in the next timeslice); this
// CPU has run the whole instruction by then. The masks, the 58XXs' reset
// and the sound enable are read only by timers' callbacks, so they can
// wait for it too.
static void latch_cb(int param) { latch_out(param >> 1, param & 1); }

static void latch_w(int offset) {       // ls259 write_a0
	int bit = offset >> 1, d = offset & 1;
	if (((mp_latch >> bit) & 1) == d) return;
	mp_latch ^= (uint8_t)(1u << bit);
	sched_timer_set(write_delay(), latch_cb, bit << 1 | d);
}

static void latch_reset(void) {         // device_reset: clear_outputs(0)
	uint8_t changed = mp_latch;
	mp_latch = 0;
	for (int bit = 0; bit < 8; bit++)
		if (changed & (1u << bit)) latch_out(bit, 0);
}

static void snd50(int param) { (void)param; }
static void scan0(int param) { (void)param; sched_timer_adjust(tm_scan0, FRAME_PERIOD, 0, SCHED_NEVER); }
static void vbend(int param) { (void)param; }
static void io_timer(int chip) { io_run(chip); }

static void machine_reset(void);

static void vbstart(int param) {
	(void)param;
	mp_frames++;
	if (mp_on_vblank) mp_on_vblank();   // frame_update: MAME draws the screen, Lua's frame_done
	if (--watchdog == 0) {               // the watchdog's VBLANK callback (8 VBLANKs)
		mp_watchdog_fired++;
		watchdog = 8;
		machine_reset();                 // schedule_soft_reset (MAME's is at the end of the slice)
		return;
	}
	// vblank_irq
	if (main_irq_mask) sched_set_input(L_MAIN_IRQ, SCHED_ASSERT_LINE);
	if (!io[0].reset) sched_timer_adjust(tm_io[0], SCHED_USEC(50), 0, SCHED_NEVER);
	if (!io[1].reset) sched_timer_adjust(tm_io[1], SCHED_USEC(50), 1, SCHED_NEVER);
	if (sub_irq_mask) sched_set_input(L_SUB_IRQ, SCHED_ASSERT_LINE);
	sched_timer_adjust(tm_vbstart, FRAME_PERIOD, 0, SCHED_NEVER);
	sched_timer_adjust(tm_vbend, VBLANK_PERIOD, 0, SCHED_NEVER);
}

// ---- the graphics (mappy_v.cpp) ----------------------------------------------------------
static int gfxbit(const uint8_t *region, unsigned bit) { return (region[bit >> 3] >> (7 - (bit & 7))) & 1; }

static void decode(const mp_roms_t *r) {
	// tiles: charlayout, 2 bits (planes at 0 and 4), ROMREGION_INVERT
	static uint8_t inv[16];
	for (int t = 0; t < 256; t++) {
		for (int k = 0; k < 16; k++) inv[k] = (uint8_t)~r->tiles[t * 16 + k];
		for (int y = 0; y < 8; y++) {
			uint16_t row = 0;
			for (int x = 0; x < 8; x++) {
				unsigned xo = x < 4 ? 64 + (unsigned)x : (unsigned)x - 4;
				unsigned b = (unsigned)y * 8 + xo;
				int p = gfxbit(inv, b) << 1 | gfxbit(inv, b + 4);
				row |= (uint16_t)(p << (2 * x));
			}
			mp_tile_rows[t][y] = row;
		}
	}
	// sprites: spritelayout_4bpp, the two ROMs' bytes interleaved
	static uint8_t sp[128];
	static const unsigned xo[16] = {0, 1, 2, 3, 128, 129, 130, 131, 256, 257, 258, 259, 384, 385, 386, 387};
	for (int s = 0; s < 128; s++) {
		for (int k = 0; k < 64; k++) {
			sp[2 * k] = r->sprites0[s * 64 + k];
			sp[2 * k + 1] = r->sprites1[s * 64 + k];
		}
		for (int y = 0; y < 16; y++) {
			unsigned yo = y < 8 ? (unsigned)y * 16 : 512 + (unsigned)(y - 8) * 16;
			uint64_t row = 0;
			for (int x = 0; x < 16; x++) {
				unsigned b = yo + xo[x];
				int p = gfxbit(sp, b) << 3 | gfxbit(sp, b + 4) << 2 | gfxbit(sp, b + 8) << 1 | gfxbit(sp, b + 12);
				row |= (uint64_t)p << (4 * x);
			}
			mp_sprite_rows[s][y] = row;
		}
	}
}

// compute_resistor_weights (resnet.cpp) for one network without pull-ups or pull-downs
static void weights(int n, const int *res, double *w) {
	for (int i = 0; i < n; i++) {
		double r0 = 1.0 / 1e12, r1 = 1.0 / 1e12;
		for (int j = 0; j < n; j++) {
			if (j == i) r1 += 1.0 / res[j];
			else r0 += 1.0 / res[j];
		}
		r0 = 1.0 / r0;
		r1 = 1.0 / r1;
		w[i] = 255.0 * r0 / (r1 + r0);
	}
}

static void palette(const uint8_t *prom) {
	static const int res[3] = {1000, 470, 220};
	double rw[3], gw[3], bw[2];
	weights(3, res, rw);
	weights(3, res, gw);
	weights(2, res + 1, bw);
	double mr = rw[0] + rw[1] + rw[2], mb = bw[0] + bw[1];
	double scale = 255.0 / (mr > mb ? mr : mb);
	for (int i = 0; i < 3; i++) { rw[i] *= scale; gw[i] *= scale; }
	for (int i = 0; i < 2; i++) bw[i] *= scale;
	for (int i = 0; i < 32; i++) {
		int v = prom[i];
		int r = (int)(rw[0] * (v & 1) + rw[1] * ((v >> 1) & 1) + rw[2] * ((v >> 2) & 1) + 0.5);
		int g = (int)(gw[0] * ((v >> 3) & 1) + gw[1] * ((v >> 4) & 1) + gw[2] * ((v >> 5) & 1) + 0.5);
		int b = (int)(bw[0] * ((v >> 6) & 1) + bw[1] * ((v >> 7) & 1) + 0.5);
		mp_pal[i] = (uint32_t)(r << 16 | g << 8 | b);
	}
	for (int i = 0; i < 256; i++) {
		mp_tile_ctab[i] = prom[0x20 + i] & 0x0f;
		mp_spr_ctab[i] = prom[0x120 + i] & 0x0f;
		mp_rgb[i] = mp_pal[0x10 + mp_tile_ctab[i]];
		mp_rgb[256 + i] = mp_pal[mp_spr_ctab[i]];
	}
}

int mp_tile_index(int col, int row) {  // mappy_tilemap_scan: 36 columns, 60 rows
	col -= 2;
	if (col & 0x20) {
		if (row & 0x20) return 0x7ff;
		return ((row + 2) & 0x0f) + (row & 0x10) + ((col & 3) << 5) + 0x780;
	}
	return col + (row << 5);
}

void mp_render(uint16_t *px) {
	static uint8_t hi[MP_W * MP_H];     // the pixel is a category 1 tile's
	for (int y = 0; y < MP_H; y++) {
		for (int x = 0; x < MP_W; x++) {
			int col = x >> 3;
			int ty = y + ((col >= 2 && col < 34) ? mp_scroll : 0);
			if (ty >= 480) ty -= 480;
			int i = mp_tile_index(col, ty >> 3);
			int code = mp_mem[i], attr = mp_mem[i + 0x800];
			int p = (mp_tile_rows[code][ty & 7] >> (2 * (x & 7))) & 3;
			int pen = (attr & 0x3f) * 4 + p;
			px[y * MP_W + x] = (uint16_t)pen;
			hi[y * MP_W + x] = (attr & 0x40) && mp_tile_ctab[pen] != 15;
		}
	}
	const uint8_t *s1 = mp_mem + 0x1780, *s2 = mp_mem + 0x1f80, *s3 = mp_mem + 0x2780;
	static const uint8_t gfx_offs[2][2] = {{0, 1}, {2, 3}};
	for (int offs = 0; offs < 0x80; offs += 2) {
		if (s3[offs + 1] & 2) continue;
		int sprite = s1[offs];
		int color = s1[offs + 1] & 15;
		int sx = s2[offs + 1] + 0x100 * (s3[offs + 1] & 1) - 40;
		int sy = 256 - s2[offs] + 1;
		int flipx = s3[offs] & 1, flipy = (s3[offs] >> 1) & 1;
		int sizex = (s3[offs] >> 2) & 1, sizey = (s3[offs] >> 3) & 1;
		sprite &= ~sizex;
		sprite &= ~(sizey << 1);
		sy -= 16 * sizey;
		sy = (sy & 0xff) - 32;
		if (MP_FLIP) { flipx ^= 1; flipy ^= 1; }
		for (int yy = 0; yy <= sizey; yy++)
			for (int xx = 0; xx <= sizex; xx++) {
				int code = (sprite + gfx_offs[yy ^ (sizey * flipy)][xx ^ (sizex * flipx)]) & 127;
				int ox = sx + 16 * xx, oy = sy + 16 * yy;
				for (int r = 0; r < 16; r++) {
					int y = oy + r;
					if (y < 0 || y >= MP_H) continue;
					uint64_t row = mp_sprite_rows[code][flipy ? 15 - r : r];
					for (int c = 0; c < 16; c++) {
						int x = ox + c;
						if (x < 0 || x >= MP_W) continue;
						int p = (int)(row >> (4 * (flipx ? 15 - c : c))) & 15;
						if (mp_spr_ctab[color * 16 + p] == 15) continue;
						if (!hi[y * MP_W + x]) px[y * MP_W + x] = (uint16_t)(256 + color * 16 + p);
					}
				}
			}
	}
}

// ---- power on and reset ------------------------------------------------------------------
static void machine_reset(void) {
	// device order: maincpu, sub, mainlatch, watchdog, ..., namco 15XX, namcoio 1 and 2
	sched_reset_cpu(D_MAIN);
	cpu_reset(D_MAIN);
	sched_reset_cpu(D_SUB);
	cpu_reset(D_SUB);
	latch_reset();
	watchdog = 8;
	for (int chip = 0; chip < 2; chip++) {
		memset(mp_io[chip], 0, sizeof mp_io[chip]);
		if (!io[chip].reset) {
			io_set_reset_line(chip, 1);
			io_set_reset_line(chip, 0);
		}
	}
}

void mp_init(const mp_roms_t *roms) {
	rom_main = roms->maincpu;
	rom_sub = roms->sub;
	decode(roms);
	palette(roms->proms);
	memset(mp_mem, 0, sizeof mp_mem);
	memset(mp_snd, 0, sizeof mp_snd);
	memset(mp_io, 0, sizeof mp_io);
	memset(io, 0, sizeof io);
	memset(mp_tile_dirty, 0xff, sizeof mp_tile_dirty);
	mp_scroll = 0;
	mp_latch = 0xff;                     // the 74259's device_start
	mp_frames = 0;
	mp_watchdog_fired = 0;
	main_irq_mask = sub_irq_mask = 0;
	dipmux_select = 0;
	m6809_init(&cpu_main);
	m6809_init(&cpu_sub);

	sched_init();
	sched_cpu(D_MAIN, MP_CLOCK, 1, 1);   // MC6809E: a cycle a clock
	sched_cpu(D_SUB, MP_CLOCK, 1, 1);
	sched_max_quantum(SCHED_HZ(6000));
	sched_line(L_MAIN_IRQ, D_MAIN, SCHED_LINE_NORMAL, 0, main_irq_set);
	sched_line(L_SUB_IRQ, D_SUB, SCHED_LINE_NORMAL, 0, sub_irq_set);
	sched_line(L_SUB_RESET, D_SUB, SCHED_LINE_RESET, 0, NULL);
	tm_snd50 = sched_timer_alloc(snd50);
	tm_vbstart = sched_timer_alloc(vbstart);
	tm_vbend = sched_timer_alloc(vbend);
	tm_scan0 = sched_timer_alloc(scan0);
	tm_io[0] = sched_timer_alloc(io_timer);
	tm_io[1] = sched_timer_alloc(io_timer);
	// the order MAME sets them: the sound manager's, then the screen's
	sched_timer_adjust(tm_snd50, SCHED_HZ(50), 0, SCHED_HZ(50));
	sched_timer_adjust(tm_vbstart, FRAME_PERIOD, 0, SCHED_NEVER);
	sched_timer_adjust(tm_scan0, VBLANK_PERIOD, 0, SCHED_NEVER);
	mp_snd_enable(0, 1);                 // namco_audio_device::device_start
	machine_reset();
}

uint32_t mp_run_to(uint64_t until) {
	uint32_t f = mp_frames;
	sched_run_to(until, MP_CLOCK);
	return mp_frames - f;
}

uint64_t mp_time(void) { return sched_ticks(sched_base(), MP_CLOCK); }

void mp_frame(void) {
	uint32_t f = mp_frames;
	while (mp_frames == f) sched_timeslice();
}

uint64_t mp_cycles(int cpu) { return m6809_now(cpu ? &cpu_sub : &cpu_main); }

void mp_regs(int cpu, uint16_t r[6]) {
	const m6809_t *c = cpu ? &cpu_sub : &cpu_main;
	r[0] = c->pc;
	r[1] = c->s;
	r[2] = c->u;
	r[3] = c->x;
	r[4] = c->y;
	r[5] = (uint16_t)(c->a << 8 | c->b);
}

uint8_t mp_regs8(int cpu, int which) {
	const m6809_t *c = cpu ? &cpu_sub : &cpu_main;
	return which ? c->cc : c->dp;
}

uint8_t mp_peek(int cpu, uint16_t a) { return cpu ? sub_rd(a) : main_rd(a); }
