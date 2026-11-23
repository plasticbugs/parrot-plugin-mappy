// mprun.c -- see mprun.h.
#include "mprun.h"

#include <string.h>

static mprun_t *running;

// each VBLANK's board, for this list or (past its end) the next
static void vblank(void) {
    mprun_t *r = running;
    if (!r) return;
    int k = r->cur ^ (mp_time() > r->target - MPRUN_LEAD);
    mpcv_board(&r->board[k]);
    r->have[k] = true;
}

void mprun_start(mprun_t *r, mpcv_t *cv) {
    memset(r, 0, sizeof *r);
    r->cv = cv;
    running = r;
    mp_on_vblank = vblank;
    r->target = mp_time();
}

size_t mprun_list(mprun_t *r, uint8_t *out) {
    r->lists++;
    r->target = ((uint64_t)r->lists * MPCV_LIST_CYCLES_Q16 >> 16) + MPRUN_LEAD;
    if (r->have[r->cur ^ 1]) r->cur ^= 1;       // the last run passed this list's VBLANK
    mp_run_to(r->target);
    if (r->since < 255) r->since++;
    if (r->have[r->cur]) {
        if (!r->cv->dirty || r->since >= MPRUN_SKIP) {
            mpcv_picture(r->cv, &r->board[r->cur]);
            r->shown = &r->board[r->cur];
            r->pictures++;
            r->since = 0;
        } else {
            r->waited++;
        }
        r->have[r->cur] = false;
    }
    mpsnd_pcm(r->pcm, MPCV_PCM_N);
    mpcv_sound_pcm(r->cv, r->pcm, MPCV_PCM_N);
    return mpcv_tv_frame(r->cv, out);
}
