#!/bin/sh
# Build the Windows installer (needs NSIS's makensis). Run build-windows.sh first.
# Usage: ./build-installer.sh /path/to/vJoySetup_v2.2.2.0_Win10_Win11.exe
#   (from https://github.com/BrunnerInnovation/vJoy/releases/tag/v2.2.2.0)
set -e
VJOY="$1"
VJOY_SHA256=ef569a3105cd301b89580f18f60c66b339e95296acf2c0dfcaf4b4bbf8ab68fe

echo "$VJOY_SHA256  $VJOY" | sha256sum -c -
mkdir -p installer/build
cp "$VJOY" installer/build/vJoySetup_v2.2.2.0_Win10_Win11.exe

VERSION=$(sed -n 's/^#define HORI_VERSION "\(.*\)"/\1/p' hori_win.h)
makensis -V2 -DVERSION="$VERSION" installer/flightstick2.nsi
echo "Built Flightstick2-Setup-$VERSION.exe"
