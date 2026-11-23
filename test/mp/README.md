# Mappy: how it works and how it's tested

The cart runs the arcade board the way MAME 0.288 emulates it: the main 6809, the sound 6809, the two 58XX I/O chips and the 15XX sound chip. The CPUs are the kit's `lib/m6809` and they take turns through `lib/sched`, MAME's scheduler, so both run the same cycles in the same slices as in MAME. The tests in this folder run the same game in MAME and in this code and compare RAM, the sound chip, the I/O chips and the picture at every frame, through the attract mode and a game the bot plays until its lives are gone.

## The picture

MAME shows Mappy turned on its side, 224 pixels wide and 288 high. The middle 256 lines are the mansion, which scrolls sideways through a tilemap 480 pixels wide. The rows above and below it hold the scores, the lives, the round markers and the credits.

The mansion is scaled as a whole, 5 columns of every 7 and 3 lines of every 4, from 480 by 256 to 343 by 192. The console shows 160 columns of it, from x 48 to 207, and moves that window as the arcade scrolls. Because the 7s and 4s belong to the mansion and not to the screen, a floor, a door or a trampoline looks the same wherever it is on the screen. Two columns or lines become one the way `sdk/cvscale.h` does it, so a stroke one pixel wide still shows. Only the tiles that change are scaled again.

Mappy, the cats and the items are scaled one at a time, from 16 by 16 to 12 by 12, so they keep their shape as they move. Each goes where the mansion's scaling puts its middle.

The rows above and below go in the 48-pixel margins at the arcade's size, so every letter keeps its shape:

- Left: 1UP and its score, HIGH SCORE and the high score, then the lives. In the attract mode the lives' place shows CREDIT and the count instead.
- Right: 2UP and its score, then the round markers, kept against the right edge as the arcade keeps them.

A score is 7 tiles wide. Below a million the first one is blank and the other 6 fill the margin. From a million on, each digit loses a column. `hudcheck.c` checks which columns can go: any pair from 1-2 to 5-6 keeps every stroke and gap of all ten digits, and the plugin merges 4 and 5.

The colours are the nearest TMS9918 colours in OKLab, with three picked by hand: the arcade's pink goes to magenta, its blue-green to cyan and its pale blue to white, where the nearest colour would have matched what's next to them.

## The sound

The 15XX has eight voices playing 32-step waves from the sound PROM. They're mixed as MAME mixes them at the chip's 192 kHz, then averaged down to the lists' 5,942 samples a second. A 15 Hz high-pass takes the resting level out. Over 45 seconds of the same game, the board's sound on the computer and MAME's recording come out at the same level, -23.1 dBFS, and below 6 kHz they differ by 35 dB less than the sound itself. Above that, MAME's resampler and the plain average part ways, and the console's 5,942 samples a second can't carry it anyway. The samples play through the SN76489's three tones as a DAC, from `src/mpdac3.h`, which `tools/mkdac3.py` makes from the kit's measured ladder.

The level is set so a game measures -24.1 dBFS rms with `tools/listlevel.py`. Ordinary ColecoVision cartridges measure -20.5 to -23.9. Peaks are rounded off with a soft knee from 3/4 of full scale, and about 11% of the samples with sound reach it. Each list also writes the whole sound chip, so a byte the console misreads lasts only one list.

## Lists and time

Each list carries 100 samples, and the console runs the lists back to back, so a list's length is the sample clock. A list is 60,236 cycles by the kit's header, 500 over a frame. MAME runs a sound byte a cycle faster than the header counts, so by MAME's costs the shortest list still comes to about 140 cycles over a frame and the console never waits.

Each list runs the board for its own time, 16.83 ms, so the game keeps the arcade's speed. The cart makes a new picture when the last one is on the console, or after 4 lists at most. In a game that's about 50 pictures a second. When the whole mansion changes at once, as at the start of a round, the console takes about 8 lists to catch up. While the view scrolls, every place in the mansion changes, so the console gets about 20 pictures a second and can show parts of two at once.

Both CPUs spend most of their time in a two-instruction loop, waiting for their interrupt. `src/mp.c` skips whole turns of those loops at once. Nothing else can run inside a CPU's timeslice, so this changes nothing the other CPU, the sound chip or the picture can see, and the tests against MAME still match at every frame.

At 150 MHz a list takes the cart 5.1 ms of its 16.8 ms on average and 13.4 ms at the 99th percentile, by `armcount.sh` over 3,051 lists of the attract mode and a game. The worst, 21.9 ms, is a list where the whole mansion is scaled again. The cart is a list ahead by then, so it still isn't late, and no list was late in the count. Without the wait-loop skip and the faster scaler, the average was 10.6 ms and 8 lists were late.

## The tests

- `test/run.sh` and `sim/test.sh` run the plugin on the kit's model of the console and in the simulator. With `MP_ROMS=path/to/mappy.zip` they play a game from a card with the zip in `/MAPPY`. Without it they check only the missing-ROM message. The same model was also given the files loose in `/MAPPY`, the zip as `/MAPPY.ZIP`, and a set with one file missing, which the screen names.
- `compare.sh attract` and `compare.sh game` check the board against MAME frame by frame. `mp_host.c` is the board on the computer, `mpdump.lua` records MAME's frames, and `cmpdump.py` compares them. In a game, `bot.h` plays and MAME plays back what it pressed.
- `cart_host.c` runs the cart's side on the computer: the board a list at a time, the pictures and the lists. It makes the previews, MAME's picture beside the console's.
- `hudcheck.c` checks which columns a 7-digit score can lose.
- `armcount.sh` times the plugin's code on an Arm model at 150 MHz.

They need MAME's `mappy.zip` (`MP_ROMS`). MAME runs headless. Neither the ROMs nor anything made from them goes in this repository: the tests write under `$TMPDIR`.
