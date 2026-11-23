// mprun: the cart's loop, a list at a time, shared by the plugin and the
// Mac tests so both run the board the same way. Portable. (Time Pilot's
// tprun.c, from Donkey Kong's dkrun.c.)
//
// Each list runs the board for the list's time (MPCV_LIST_T of the
// console's T: 16.77 ms, a little more than the board's 16.5 ms frame), to
// a cycle count kept from the start so nothing drifts; MPCV_PCM_N samples
// of the sound go with the list (mpsnd.c), taken MPRUN_LEAD cycles behind
// the board so the 15XX's writes for them are always made.
//
// The picture: the board at the last VBLANK in the list's time, copied at
// each VBLANK and kept for the list it belongs to (a run can pass the
// next list's VBLANK too). Mappy's frame is 16.5 ms, so about one of its
// frames in 60 is never shown (60.6 frames a second, 59.6 lists). A
// picture is made only when the console has had the last one's bytes (or
// MPRUN_SKIP lists have gone by): a picture that needs more than a list
// can carry is sent whole over the lists it takes, and the game runs on
// at its own speed meanwhile.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp.h"
#include "mpcv.h"
#include "mpsnd.h"

#define MPRUN_LEAD 1300                 // board cycles: 0.85 ms, about 5 samples
#ifndef MPRUN_SKIP
#define MPRUN_SKIP 4                    // a picture at least every 4 lists, sent or not
#endif

typedef struct {
    mpcv_t *cv;
    uint32_t lists;                     // since the start
    uint64_t target;                    // the board cycle this list runs to
    uint32_t pictures;
    uint32_t waited;                    // lists that made no picture (the last one's bytes not all sent)
    uint8_t since;                      // lists since the last picture
    int16_t pcm[MPCV_PCM_N];
    mpcv_board_t board[2];              // the board at the VBLANK for this list ([cur]), and one past its end
    bool have[2];
    uint8_t cur;
    const mpcv_board_t *shown;          // the one the last picture was made from (for the tests)
} mprun_t;

// the board started (mpsnd_init and mp_init done)
void mprun_start(mprun_t *r, mpcv_t *cv);
// one list: the board for its time, then the list into out; returns its length
size_t mprun_list(mprun_t *r, uint8_t *out);
