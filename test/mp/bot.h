// bot.h -- a player for the tests against MAME: it holds left or right
// for a while (a little longer each way in turn, so it gets about the
// mansion), lets go now and then, and taps the button (the doors). Only
// what it pressed matters: mp_host records it and MAME plays the same.
#pragma once
#include <stdint.h>

static inline void bot_play(int n, uint8_t *p1, uint8_t *buttons) {
	static uint32_t seed = 12345;
	static int left = 0, dir = 0x02;
	if (left-- <= 0) {
		seed = seed * 1103515245u + 12345u;
		int r = (int)(seed >> 16);
		left = 20 + r % 150;
		dir = (r & 0x300) == 0 ? 0 : (r & 0x80) ? 0x02 : 0x08;
	}
	*p1 = (uint8_t)dir;
	*buttons = (uint8_t)((n % 53) < 3 ? 0x01 : 0x00);
}
