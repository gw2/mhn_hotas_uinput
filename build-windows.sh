#!/bin/sh
# Cross-compile hori.exe and hori-test.exe for Windows (needs mingw-w64).
# Usage: ./build-windows.sh /path/to/extracted/libusb-1.0.27   (the libusb Windows release .7z)
set -e
LIBUSB="$1"
CFLAGS="-Wall -Wextra -Wno-unused-parameter -O2 -I$LIBUSB/include"
LIBS="$LIBUSB/MinGW64/static/libusb-1.0.a -lwinmm -lgdi32 -lcomctl32 -lshell32 -lurlmon -lbcrypt -lole32 -static"
SRC="hori_win.c hori_gui.c hori_setup.c"

x86_64-w64-mingw32-windres hori.rc -O coff -o hori_res.o

# Console driver (add -g for the test window)
x86_64-w64-mingw32-gcc $CFLAGS -o hori.exe $SRC hori_res.o $LIBS
# Test window, no console: double-click to open
x86_64-w64-mingw32-gcc $CFLAGS -DHORI_GUI_DEFAULT -mwindows -o hori-test.exe $SRC hori_res.o $LIBS
rm -f hori_res.o
