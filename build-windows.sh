#!/bin/sh
# Cross-compile hori.exe, hori-test.exe and hori-driver.exe for Windows (needs mingw-w64).
# Usage: ./build-windows.sh /path/to/extracted/libusb-1.0.27   (the libusb Windows release .7z)
set -e
LIBUSB="$1"
CFLAGS="-Wall -Wextra -Wno-unused-parameter -O2 -I$LIBUSB/include"
LIBS="$LIBUSB/MinGW64/static/libusb-1.0.a -lwinmm -lgdi32 -lcomctl32 -lshell32 -lurlmon -lbcrypt -lole32 -static"
SRC="hori_win.c hori_gui.c hori_setup.c hori_about.c"

x86_64-w64-mingw32-windres hori.rc -O coff -o hori_res.o

# Console driver (add -g for the test window)
x86_64-w64-mingw32-gcc $CFLAGS -o hori.exe $SRC hori_res.o $LIBS
# Test window, no console: double-click to open
x86_64-w64-mingw32-gcc $CFLAGS -DHORI_GUI_DEFAULT -mwindows -o hori-test.exe $SRC hori_res.o $LIBS
rm -f hori_res.o

# One-click WinUSB driver install, with libwdi's pki.c (third_party/libwdi, LGPLv3)
x86_64-w64-mingw32-windres hori_driver.rc -O coff -o hori_driver_res.o
x86_64-w64-mingw32-gcc -Wall -O2 -Ithird_party/libwdi_shim -Ithird_party/libwdi -o hori-driver.exe \
	hori_driver.c third_party/libwdi/pki.c hori_driver_res.o -lsetupapi -lnewdev -lshell32 -ladvapi32 -lcrypt32 -static
rm -f hori_driver_res.o
