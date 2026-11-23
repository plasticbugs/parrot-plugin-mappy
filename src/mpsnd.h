// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles
//
// mpsnd: Mappy's sound chip, the Namco 15XX, as MAME 0.288's namco.cpp
// runs it, for the cart (portable). Eight voices, each a 32-step 4-bit
// waveform from the sound PROM (mp1-3) at a 20-bit frequency, a volume of
// 0-15, summed at 192 kHz: (sample - 8) * volume / 1024 a voice, as MAME
// mixes them. The board (mp.c) hands over each register write and the
// sound enable with their times; the cart takes the samples the lists
// play (100 a list, 5,962 a second), each the mean of the 192 kHz samples
// it spans, the resting level taken off (a pole at 15 Hz) and scaled to
// the DAC's levels (mpcv_sound_pcm: +-32767 is +-511 levels).
#pragma once
#include <stdbool.h>
#include <stdint.h>

// the samples' scale: MPSND_GAIN DAC levels for 1.0 of MAME's output (its
// 32768). test/mp/level.sh measures the loudness against a cartridge's.
#ifndef MPSND_GAIN
#define MPSND_GAIN 1400.0f
#endif

void mpsnd_init(const uint8_t *wave_prom);      // power on (before mp_init)
// the next n samples at the lists' rate, in the DAC's units (the board must
// have run far enough for their writes)
void mpsnd_pcm(int16_t *out, int n);
// the 15XX's own output over the next n of its ticks (192 kHz), summed:
// MAME's value times 1024 times n (tests: a WAV at any rate)
int32_t mpsnd_run(int n);
uint64_t mpsnd_tick(void);              // the next tick the stream makes
extern uint32_t mpsnd_samples;          // made so far (the lists')
extern uint32_t mpsnd_dropped;          // writes the queue had no room for (should stay 0)
