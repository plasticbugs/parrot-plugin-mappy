# Mappy (MAPPY.CVP): Namco's 1983 arcade game on the ColecoVision. The cart
# runs the arcade board (two 6809s, two 58XX I/O chips and the 15XX sound
# chip: src/mp.c, src/mpsnd.c, from MAME 0.288) from your own ROMs on the
# card (/MAPPY), and src/mpcv.c sends the picture and the sound to the
# console as the frame kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/MAPPY.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Started from tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(mappy C CXX ASM)
parrotcart_init()

# The same source makes the same file, whenever it's built: __DATE__ and
# __TIME__ come from SOURCE_DATE_EPOCH, fixed here (2026-11-23).
set(BUILD_EPOCH 1795392000)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(EMU src/mp.c src/mpsnd.c src/mprun.c)
set(CARD src/mpcard.c src/mproms.c ${PARROTCART_SDK}/sdk/swzip.c)
add_executable(mappy src/main.c src/mpcv.c ${EMU} ${CARD} ${PARROTCART_SDK}/midihello/vdp.c)
# The lists run back to back: their length is the sound's sample clock
# (porting.md, "Lists back to back")
parrotcart_frames(mappy BACK_TO_BACK)
target_include_directories(mappy PRIVATE src ${PARROTCART_SDK}/sdplayer)
# The emulation -O3, its state not split into a section a variable (every
# use would need its own address loaded); the picture (mpcv.c) -O2 for
# room; the card's code runs once, at the start: built for size.
# test/mp/armcount.sh builds each file the same way.
set_source_files_properties(src/main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
set_source_files_properties(${EMU} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O3;-fno-data-sections")
set_source_files_properties(src/mpcv.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O2")
set_source_files_properties(${CARD} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-Os")
# core 0's stack 4K, core 1's 2K
target_compile_definitions(mappy PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
cvcart_features(mappy PADS CARD)

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin(mappy ID parrotcart.mappy NAME "Mappy" VERSION 0.1.0 BY "Parrot Cart"
              ICON src/icon.txt README README.md FILE MAPPY.CVP
              HOME /MAPPY USES usb-host)
