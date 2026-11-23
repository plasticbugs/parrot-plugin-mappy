// mpcard.h -- the ROMs on the card, through fsro: loose or zipped in
// /MAPPY (MAME's mappy.zip, as it is), or /MAPPY.ZIP. Portable: the
// plugin and the kit's model of the console (test/run.sh, on disk images)
// share it.
// (Time Pilot's tpcard.h.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cvsd.h"                       // (fs_t: the SDK's sdplayer/fsro.h)
#include "mproms.h"

#define MPCARD_HOME "/MAPPY"
#define MPCARD_ZIP  "/MAPPY.ZIP"

// Every file in MPCARD_HOME, then MPCARD_ZIP, until all are found: each
// ROM to its place (mproms.h). A zip is never read whole: its directory,
// then each wanted entry's packed bytes into buf and unpacked after them.
// buf: max bytes (the directory, and a packed ROM with the ROM after it,
// must fit); tick: called between sectors (the plugin serves the
// console), or NULL. Returns mproms' found bits.
uint32_t mpcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void));
