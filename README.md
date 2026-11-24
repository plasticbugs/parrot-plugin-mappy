# Mappy

This plugin lets Parrot Cart play Namco's 1983 Mappy arcade game on the ColecoVision. The cart emulates the arcade board, ported from MAME. You supply the game's ROMs from your own copy of MAME's `mappy` set.

## What goes on the card

1. Copy `MAPPY.CVP` into the `/PLUGINS` folder.
2. Make a folder named `/MAPPY` and put MAME's `mappy.zip` in it as it is, or the 11 files from it loose. You can also put the zip in the card's top folder as `/MAPPY.ZIP`.
3. Start Mappy from the menu.

The plugin knows each ROM by its size and contents, so the file names don't matter. If a ROM is missing, the screen names it. Only MAME's `mappy` set works. The Japanese set has two different ROMs.

## Controls

On the ColecoVision controller:

- The joystick runs left and right.
- Either fire button opens and shuts doors.
- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and keypad 2 a two-player game.
- RESET on the console goes back to the menu, and so does holding both fire buttons and \* for 2 seconds (Select and Start on a USB pad).

On a USB pad, through a powered hub:

- The D-pad or the left stick runs.
- Any face button works the doors.
- Minus adds a coin, and plus starts a one-player game.

## Picture and sound

The arcade screen is taller than the TV's, so the mansion is scaled down to fit in the middle. It's scaled as a whole, not as the screen shows it, so the floors and doors keep their shape while the screen scrolls. Mappy and the cats are scaled one at a time and keep their shape too. While the mansion scrolls, the whole picture changes, so the console shows fewer new pictures a second and can show parts of two at once.

Text in the mansion is drawn 6 pixels a letter, so every letter keeps its strokes, and the points keep theirs too. The MAPPY logo on the title screen is scaled with the mansion. The ColecoVision can show only two colours in each 8-pixel run, so the logo drops its dark blue outline and keeps the letters' colours first.

The scores, lives, rounds and credits go in the margins on both sides at the arcade's size, so every letter stays readable.

The board's sound chip has eight voices. They're mixed as in MAME and played as samples on the ColecoVision's sound chip, at about the loudness of an ordinary cartridge.

High scores last until you go back to the menu.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Nicola Salmoria, for the Mappy driver, its video and the Namco I/O chips.
- Nicola Salmoria and Aaron Giles, for the Namco sound chip.
- Nathan Woods, for the 6809 CPU.
- Aaron Giles, for MAME's scheduler, screen timing and watchdog.
- AJR, for the LS259 latch and the LS157 multiplexer.
- Couriersud, for the resistor networks behind the palette.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin is by Parrot Cart, and it's free. Its source is here: https://github.com/plasticbugs/parrot-plugin-mappy

Mappy is a trademark of Bandai Namco. Parrot Cart isn't connected with them. This plugin doesn't include any game ROMs.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/MAPPY.CVP`. On Ubuntu, `tools/ci/setup.sh` installs the
same tools GitHub uses to build each release.

`test/mp/README.md` explains how the picture and sound are made, and the
tests against MAME. The rest of this repository is the Parrot Cart starter
kit, and `docs/` is its manual.

## License

The plugin is under the BSD 3-Clause license (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
