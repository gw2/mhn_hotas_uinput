# mhn_hotas_uinput
Device driver for Mitsubishi Hori/Namco Flightstick 2 HOTAS (Ace Combat 5)

Uses uinput and libusb-1.0

It's developed/tested with Linux 6.10.12

USB ID is 06d3:0f10

This is a userspace implementation of my kernel driver at https://github.com/danieloneill/mhn_hotas

## Credits

Used https://www.tamanegi.org/prog/hfsd/ as reference, and copied the data structs for control messages.

## Notes

* The original button and axis numbering is unchanged, so existing bindings keep working. New axes are added after the existing ones.
* M1/M2/M3 is supported, picked with `-m` (see Options). The switch briefly reads "no mode" while it's moving between positions. That reading is now ignored, so it no longer sends stray events while you're binding.
  * `buttons` (default): 3 extra buttons (BTN_TRIGGER_HAPPY9-11), and the selected mode's button is held down.
  * `axis`: one ABS_MISC axis, with 0 = M1, 1 = M2, 2 = M3.
  * `shift`: every button sends a different code in M2 and M3, which gives you 3 banks of 20 buttons. M1 uses the original codes. Anything held down while you flip the switch is released first, so nothing gets stuck.
* A and B are analog. Their pressure is reported on ABS_VOLUME (A) and ABS_PROFILE (B), 0 = released and 255 = full. They still act as digital buttons BTN_A/BTN_B too. Change the press threshold with `-t`. There's some hysteresis, so the buttons don't flicker at the threshold.
* Rumble works through standard force feedback (FF_RUMBLE). It sends vendor request 0x0c, as in hfsd. The stick has only one motor, so it uses whichever of the two rumble strengths is stronger. Test it with `fftest /dev/input/eventN`. Turn it off with `-R`.
* Dials: the original Windows driver (hfsd) never read the dials either, and there's no known USB report for them. On some units they may sit on an extra mouse/keyboard USB interface. `-k` claims any such interface and turns boot-mouse X/Y motion into two absolute axes (ABS_THROTTLE, ABS_WHEEL). It's experimental. If your unit has no extra interface it says so and does nothing. Run with `-d -k` and turn the dials to see what, if anything, comes in.
* D-PAD1 is split into 4 buttons, D-PAD3 is split into 3 buttons. D-PAD2 is split into 4 buttons.
* HAT +PUSH reads the same bit hfsd uses. If it does nothing for you, run with `-d` and press it. If the `vr00` bytes don't change, the switch itself isn't reporting.
* Ctrl+C / SIGTERM now remove the virtual device and stop rumble cleanly.

## Options

```
  -m MODE   off | buttons | axis | shift   (M1/M2/M3 handling, default buttons)
  -t N      A/B press threshold on the 0-255 pressure scale (default 20)
  -R        Disable rumble
  -k        Experimental: read the dials from any extra mouse/keyboard interface
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
