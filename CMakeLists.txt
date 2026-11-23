# Mappy: Namco's 1983 arcade game Mappy on the ColecoVision, run from your own MAME ROMs.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/MAPPY.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Made by tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(mappy C CXX ASM)
parrotcart_init()

add_executable(mappy src/main.c)
parrotcart_frames(mappy)           # a whole picture every TV frame: the frame kernel

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
cvcart_features(mappy PADS)

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin(mappy ID parrotcart.mappy NAME "Mappy" VERSION 0.1.0 BY "Parrot Cart"
              ICON src/icon.txt README README.md FILE MAPPY.CVP
              HOME /MAPPY)
