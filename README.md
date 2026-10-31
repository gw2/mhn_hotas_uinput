# mhn_hotas_uinput
Device driver for Mitsubishi Hori/Namco Flightstick 2 HOTAS (Ace Combat 5)

Uses uinput and libusb-1.0

It's developed/tested with Linux 6.10.12

USB ID is 06d3:0f10

This is a userspace implementation of my kernel driver at https://github.com/danieloneill/mhn_hotas

## Credits

Used https://www.tamanegi.org/prog/hfsd/ as reference, and copied the data structs for control messages.

This fork ([gw2/mhn_hotas_uinput](https://github.com/gw2/mhn_hotas_uinput)) adds a Windows version and builds on:

* [mhn_hotas_uinput](https://github.com/danieloneill/mhn_hotas_uinput) by Daniel O'Neill: the original driver (MIT, see `LICENSE`).
* [hfsd](https://www.tamanegi.org/prog/hfsd/) by tamanegi.org: the USB protocol (free, no warranty, no restrictions).
* [libusb](https://libusb.info) 1.0.27: built into the Windows exes (LGPL 2.1 or later, see `LICENSE-libusb.txt`).
* [vJoy](https://github.com/BrunnerInnovation/vJoy) by Shaul Eizikovich, maintained by Brunner Elektronik AG (MIT): its official installer is included in the Windows installer.
* [libwdi](https://github.com/pbatard/libwdi) by Pete Batard (LGPLv3): its `pki.c` is built into `hori-driver.exe`, which installs the stick's WinUSB driver the way [Zadig](https://zadig.akeo.ie) does (see `third_party/libwdi`).
* [NSIS](https://nsis.sourceforge.io) (zlib; LZMA module CPL 1.0): builds the Windows installer.

Details, including what each is used for: `THIRD-PARTY-NOTICES.txt`. None of these projects offers a donation link.

## Notes

* The original button and axis numbering is unchanged, so existing bindings keep working. New axes are added after the existing ones.
* M1/M2/M3 is supported, picked with `-m` (see Options). The switch briefly reads "no mode" while it's moving between positions. That reading is now ignored, so it no longer sends stray events while you're binding.
  * `buttons` (default): 3 extra buttons (BTN_TRIGGER_HAPPY9-11), and the selected mode's button is held down.
  * `axis`: one ABS_MISC axis, with 0 = M1, 1 = M2, 2 = M3.
  * `shift`: every button sends a different code in M2 and M3, which gives you 3 banks of 20 buttons. M1 uses the original codes. Anything held down while you flip the switch is released first, so nothing gets stuck.
* A and B are analog. Their pressure is reported on ABS_VOLUME (A) and ABS_PROFILE (B), 0 = released and 255 = full. They still act as digital buttons BTN_A/BTN_B too. Change the press threshold with `-t`. There's some hysteresis, so the buttons don't flicker at the threshold.
* Rumble works through standard force feedback (FF_RUMBLE). It sends vendor request 0x0c, as in hfsd. The stick has only one motor, so it uses whichever of the two rumble strengths is stronger. Test it with `fftest /dev/input/eventN`. Turn it off with `-R`.
* Dials: they're mechanical adjusters and send nothing over USB. Tested on real hardware: turning them changes no axis, no button bit and no vendor request, not even with the stick, throttle or rudder held off-centre. The stick has a single USB interface, so there's no hidden mouse/keyboard device either. They can't be mapped.
* D-PAD1 is split into 4 buttons, D-PAD3 is split into 3 buttons. D-PAD2 is split into 4 buttons.
* HAT +PUSH reads the same bit hfsd uses. If it does nothing for you, run with `-d` and press it. If the `vr00` bytes don't change, the switch itself isn't reporting.
* Ctrl+C / SIGTERM now remove the virtual device and stop rumble cleanly.

## Options

```
  -m MODE   off | buttons | axis | shift   (M1/M2/M3 handling, default buttons)
  -t N      A/B press threshold on the 0-255 pressure scale (default 20)
  -R        Disable rumble
  -d        Debug: print USB descriptors and raw reports as they change
  -P        Debug: probe vendor IN requests 0x00-0x1f once at connect (read-only)
```

Keep in mind that for Proton/Wine games Steam likes to emulate an Xbox 360 controller and override HID input devices in the name of compatibility, which will ruin a lot of functionality in this case.

## Usage

```
$ make
cc -o hori hori.c -lusb-1.0

$ sudo ./hori
hori_init_existing: Device registered.
Mitsubishi HORI/Namco Flightstick 2 driver loaded.
hori_poll_ir: Expected 8 bytes, received 4 bytes.
```

Test it with "jstest" or "jstest-gtk"

(Don't mind if the axiseses are a bit confused in the test app, binding them in game works just fine.)

It's safe to ignore that "Expected 8 bytes" warning before the device settles.

If you want to be able to execute it as a regular user (such as on session startup in KDE):

```
$ sudo cp hori /usr/local/bin
$ sudo chmod u+s /usr/local/bin/hori
```
