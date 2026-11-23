// SPDX-License-Identifier: MIT
// test/mp/bench.c -- the cart's work for each list, for the kit's
// tools/armcount (test/mp/armcount.sh builds and runs it): the board for a
// list's time with the same mprun.c the plugin runs, its picture and its
// sound made into the list. A coin at list 1000 (after the board's own
// tests), a start at 1060, then the tests' bot plays (bot.h), as
// test/mp/compare.sh has MAME do. The set comes in bench_data (armcount.py
// --load), as armcount.sh lays it out.
#include <stdint.h>
#include <string.h>

#include "mp.h"
#include "mpcv.h"
#include "mprun.h"
#include "mpsnd.h"
#include "bot.h"

// main 24K, sub 8K, tiles 4K, sprites 8K + 8K, PROMs 0x220, the sound PROM 0x100
#define B_MAIN 0
#define B_SUB 0x6000
#define B_TILES 0x8000
#define B_SPR0 0x9000
#define B_SPR1 0xB000
#define B_PROMS 0xD000
#define B_WAVE 0xD220
uint8_t bench_data[0xD320];

static mpcv_t cv;
static mprun_t run;
static uint8_t list[MPCV_LIST_MAX];
static uint32_t lists;

static void bench_list(void) {
	lists++;
	uint8_t p1 = 0, b = 0, coin = 0;
	if (lists >= 1000 && lists < 1006) coin = 1;
	if (lists >= 1060 && lists < 1066) b = 0x04;
	if (lists >= 1120) bot_play((int)lists, &p1, &b);
	mp_in.coins = (uint8_t)(0x0f & ~coin);
	mp_in.p1 = (uint8_t)(0x0f & ~p1);
	mp_in.buttons = (uint8_t)(0x0f & ~b);
	(void)mprun_list(&run, list);
}

void __attribute__((noinline)) bench_mark(void) { __asm volatile("" ::: "memory"); }

int main(void) {
	static const mp_roms_t roms = {bench_data + B_MAIN, bench_data + B_SUB, bench_data + B_TILES, bench_data + B_SPR0,
	                               bench_data + B_SPR1, bench_data + B_PROMS, bench_data + B_WAVE};
	mpsnd_init(roms.wave);
	mp_init(&roms);
	mpcv_init(&cv);
	mpcv_prepare();
	mprun_start(&run, &cv);
	for (;;) {
		bench_list();
		bench_mark();
	}
}

extern uint32_t __stack_top;
void __attribute__((naked, noreturn)) _start(void) {
	__asm volatile("ldr sp, =__stack_top\n bl main\n b .");
}
void *_sbrk(int n) { (void)n; return (void *)-1; }
