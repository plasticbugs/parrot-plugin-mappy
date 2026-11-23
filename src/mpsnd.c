// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles
//
// mpsnd.c -- see mpsnd.h. The voices are MAME 0.288's namco_15xx_device
// (sound/namco.cpp: namco_15xx_w, namco_update_one, sound_stream_update),
// by Nicola Salmoria, Aaron Giles and the MAME team, BSD-3-Clause. The
// samples' bins and the high-pass are Time Pilot's (Parrot Cart's
// timeplt/src/tpsnd.c).
#include "mpsnd.h"

#include <math.h>
#include <string.h>

#include "mp.h"
#include "mpcv.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SND_HOT __attribute__((section(".time_critical.mpsnd")))
#else
#define SND_HOT
#endif

#define FRACBITS 18                     // 24 kHz doubled three times to 192 kHz: 15 + 3

typedef struct {
	uint32_t freq, counter;
	uint8_t vol, sel;
} voice_t;
static voice_t voice[8];
static bool enabled;
static int8_t wave[8][32];              // the PROM's samples less 8
static uint8_t regs[64];

typedef struct {
	uint32_t tick;
	uint8_t reg, val;                   // reg 0xff: the sound enable
} ev_t;
#define QN 1024
static ev_t q[QN];
static unsigned qh, qt;                 // the next to apply, the next free
static uint64_t pos;                    // the next tick to make

uint32_t mpsnd_samples, mpsnd_dropped;

// the lists' samples: bins of 192 kHz ticks, kept as a whole part and a
// remainder so nothing drifts
#define TICKS_NUM ((uint64_t)MP_SND_RATE * MPCV_LIST_T)
#define TICKS_DEN ((uint64_t)MPCV_PCM_N * MPCV_CONSOLE_HZ)
static uint32_t bin_q, bin_r, bin_rem;
static float hp_x, hp_y, hp_a;

static void apply(const ev_t *e) {
	if (e->reg == 0xff) {
		enabled = e->val != 0;
		return;
	}
	int r = e->reg;
	regs[r] = e->val;
	voice_t *v = &voice[r >> 3];
	int ch = r >> 3;
	switch (r & 7) {
	case 2:
		v->counter &= (1u << FRACBITS) - 1;
		v->counter |= (uint32_t)(e->val & 0x1f) << FRACBITS;
		break;
	case 3:
		v->vol = e->val & 0x0f;
		break;
	case 6:
		v->sel = (e->val >> 4) & 7;
		// fall through
	case 4:
	case 5:
		v->freq = regs[ch * 8 + 4] + ((uint32_t)regs[ch * 8 + 5] << 8) + ((uint32_t)(regs[ch * 8 + 6] & 15) << 16);
		break;
	default:
		break;
	}
}

static void push(uint64_t tick, uint8_t reg, uint8_t val) {
	if (qt - qh >= QN) {                // full: the oldest now
		apply(&q[qh % QN]);
		qh++;
		mpsnd_dropped++;
	}
	ev_t *e = &q[qt % QN];
	e->tick = (uint32_t)tick;
	e->reg = reg;
	e->val = val;
	qt++;
}

void mp_snd_write(uint64_t tick, int reg, uint8_t value) { push(tick, (uint8_t)reg, value); }
void mp_snd_enable(uint64_t tick, int on) { push(tick, 0xff, (uint8_t)(on != 0)); }

void mpsnd_init(const uint8_t *prom) {
	memset(voice, 0, sizeof voice);
	memset(regs, 0, sizeof regs);
	for (int s = 0; s < 8; s++)
		for (int i = 0; i < 32; i++) wave[s][i] = (int8_t)((prom[s * 32 + i] & 0x0f) - 8);
	enabled = true;
	qh = qt = 0;
	pos = 0;
	mpsnd_samples = mpsnd_dropped = 0;
	bin_q = (uint32_t)(TICKS_NUM / TICKS_DEN);
	bin_r = (uint32_t)(TICKS_NUM % TICKS_DEN);
	bin_rem = 0;
	hp_x = hp_y = 0;
	hp_a = expf(-2.0f * 3.14159265f * 15.0f / ((float)MPCV_PCM_N * MPCV_CONSOLE_HZ / MPCV_LIST_T));
}

uint64_t mpsnd_tick(void) { return pos; }

// n ticks of the voices: the sum of (sample - 8) * volume
static SND_HOT int32_t render(int n) {
	if (!enabled) return 0;
	int32_t sum = 0;
	for (int k = 0; k < 8; k++) {
		voice_t *v = &voice[k];
		if (!v->vol) continue;
		const int8_t *w = wave[v->sel];
		uint32_t c = v->counter, f = v->freq;
		int32_t s = 0;
		if (!f) {
			s = w[(c >> FRACBITS) & 31] * n;
		} else {
			for (int i = 0; i < n; i++) {
				s += w[(c >> FRACBITS) & 31];
				c += f;
			}
		}
		v->counter = c;
		sum += s * v->vol;
	}
	return sum;
}

SND_HOT int32_t mpsnd_run(int n) {
	int32_t sum = 0;
	while (n > 0) {
		while (qh != qt && (int32_t)(q[qh % QN].tick - (uint32_t)pos) <= 0) {
			apply(&q[qh % QN]);
			qh++;
		}
		int k = n;
		if (qh != qt) {
			int32_t d = (int32_t)(q[qh % QN].tick - (uint32_t)pos);
			if (d < k) k = d;
		}
		sum += render(k);
		pos += (uint64_t)k;
		n -= k;
	}
	return sum;
}

static inline int next_bin(void) {
	int n = (int)bin_q;
	bin_rem += bin_r;
	if (bin_rem >= TICKS_DEN) {
		bin_rem -= (uint32_t)TICKS_DEN;
		n++;
	}
	return n;
}

SND_HOT void mpsnd_pcm(int16_t *out, int n) {
	for (int k = 0; k < n; k++) {
		int t = next_bin();
		float x = (float)mpsnd_run(t) * (1.0f / 1024.0f) / (float)t;
		hp_y = hp_a * (hp_y + x - hp_x);
		hp_x = x;
		float v = hp_y * MPSND_GAIN * 64.0f;
		if (v > 32767.0f) v = 32767.0f;
		if (v < -32767.0f) v = -32767.0f;
		out[k] = (int16_t)lrintf(v);
	}
	mpsnd_samples += (uint32_t)n;
}
