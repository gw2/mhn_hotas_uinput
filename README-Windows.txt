HORI/Namco Flightstick 2 (Ace Combat 5) on Windows 10/11
=========================================================

hori.exe reads the stick and feeds a vJoy virtual joystick that games see
as a normal controller.


Easiest: the installer
----------------------
Plug in the stick and run Flightstick2-Setup-<version>.exe. With one admin
prompt it:
  - installs the app to Program Files, with Start-menu shortcuts and an
    entry in Settings > Apps for uninstalling,
  - installs vJoy 2.2.2.0 (its official installer is included) if needed,
  - sets vJoy device 1 to 8 axes, 23 buttons and 3 POV hats (this replaces
    device 1's current setup; untick it on the components page to skip),
  - gives the stick Windows' built-in WinUSB driver with hori-driver.exe,
  - and, if you tick it, starts Flightstick 2 in the tray when you sign in.
Then open "Flightstick 2" from the Start menu to check it.
Settings and logs go in %APPDATA%\Flightstick2 and %LOCALAPPDATA%\Flightstick2.

Windows may warn that the installer is from an unknown publisher, because
it isn't code-signed: click "More info", then "Run anyway".

Uninstalling (Settings > Apps > Flightstick 2 driver) removes the app and
offers to remove the stick's WinUSB driver. vJoy stays, with its own
uninstaller.


About the stick's WinUSB driver (hori-driver.exe)
-------------------------------------------------
Windows has no driver for this stick. hori-driver.exe adds a small driver
package that points it at the WinUSB driver built into Windows; no driver
code of our own is installed. Windows wants driver packages signed, so,
like Zadig, it signs this one with a certificate made just for it, adds that
certificate to Windows' trusted certificates, and deletes the certificate's
private key so it can't sign anything else. The signing code is libwdi's,
the library Zadig is built on.
  hori-driver.exe install     (as admin) install it
  hori-driver.exe uninstall   (as admin) remove it and its certificate


Zip download: the Setup check
-----------------------------
If you use the zip instead of the installer, unzip it anywhere, plug in the
stick and run hori-test.exe. If anything is missing it opens the Setup
check, which checks three things and offers to fix each one:
  1. vJoy installed      -> downloads vJoy 2.2.2.0 and runs its installer
  2. vJoy device set up  -> runs vJoyConfig: 8 axes, 23 buttons, 3 POV hats
                            (this replaces vJoy device 1's current setup)
  3. Stick has WinUSB    -> runs hori-driver.exe install
                            (or, if it's missing, downloads Zadig 2.9 and
                            opens it with WinUSB selected)
Nothing is downloaded or installed until you click and say yes, and Windows
asks for admin permission for each install. Downloads come from the official
GitHub releases and are checked against a known SHA-256 fingerprint before
they run; files go in %LOCALAPPDATA%\hori-setup. You can open it any time
with "Setup check...".
A copy with a hori.ini next to the exe keeps its settings and logs in its
own folder (portable); otherwise they go in your user folders as above.

The manual steps below do the same by hand.


1. Install vJoy (the virtual joystick)
--------------------------------------
Download the signed v2.2.2.0 release from
  https://github.com/BrunnerInnovation/vJoy/releases
and run the installer. Reboot if it asks.


2. Configure the vJoy device
----------------------------
Start menu -> "Configure vJoy". For device 1:
  - Axes: tick X, Y, Z, Rx, Ry, Rz, Slider, Dial/Slider2
          (and Wheel if you want "-m axis")
  - Number of buttons: 23  (or 87 if you want "-m shift")
  - POV hat switches: Continuous, 3  (D-PAD 1, 2 and 3; with 2, D-PAD 3 is buttons)
  - Click Apply. Make sure "Enable vJoy" at the bottom left is ticked.


3. Give the stick the WinUSB driver (Zadig)
-------------------------------------------
Windows has no driver for this stick, so hori.exe talks to it through WinUSB.
  - Plug in the stick.
  - Download Zadig from https://zadig.akeo.ie and run it.
  - Options -> List All Devices.
  - Pick the device with USB ID 06D3 0F10 (it may show up as "Flight Stick 2",
    an unknown device, or "... (Interface 0)").
    Double-check the ID: picking the wrong device replaces that device's driver.
  - Target driver: WinUSB. Click "Install Driver" (or "Replace Driver").

To undo this later: Device Manager -> find the stick -> Uninstall device,
tick "Delete the driver software", then unplug and replug it.


The tray app: hori-test.exe
---------------------------
hori-test.exe is the driver with a window. While it runs it has an icon in
the system tray (by the clock; it may be under the ^ arrow), whose tooltip
says whether the stick is connected. Closing the window sends it to the
tray and games keep working; click the icon to open it again. Right-click
the icon for:
  Open Flightstick 2   the test window
  Setup check...       vJoy and the stick's driver
  Start with Windows   starts it in the tray when you sign in (shows up in
                       Settings > Apps > Startup)
  Exit                 stops the driver
Starting it again while it's running just brings the window forward.
hori-test.exe -T starts it straight into the tray. hori.exe is the same
driver in a console window, for anyone who prefers that.

Double-click hori-test.exe for a window that shows every control live on a
line drawing of the controller (buttons light up green, the hat, rudder and
throttle show their position, the M1/M2/M3 LEDs light up like the real ones),
plus whether the stick, the USB link and vJoy are each working.
Pressure-sensitive buttons fill up as you press them. It also
feeds vJoy, so you can play with it open. Use either hori.exe or
hori-test.exe, not both at the same time (only one program can own the vJoy
device).

Setup wizard
  Click "Setup wizard..." and pick what to test:
    Test everything   the analog controls, then every button (about 3 minutes)
    Analog only       stick, hat, rudder, throttle and the pressure buttons
    Buttons only      every button and the mode switch, one at a time
  Each step finishes by itself once it has seen enough; skip anything that
  doesn't work. The control being asked for is highlighted on the drawing.

  Analog: measures how much each sensor jitters with nobody touching it, the
  full range of each control, and where the stick, hat and rudder come to
  rest after springing back. It recommends dead zones (and the press
  threshold for the pressure buttons), with the reason for each.

  Buttons: asks for each control in turn (Trigger, Button C, SW-1, Launch,
  A, B, the hat push, each D-pad direction, D, ST and M1/M2/M3) and checks
  it works. Only the button asked for counts: press a different one (or
  several at once) and it tells you which and keeps waiting.
  D-PAD 3 has three positions: up, right and left (the original driver
  called up "middle"). There is no down.
  Dials: they're mechanical adjusters and send nothing over USB (tested:
  turning them changes no axis, button or data byte, even with the stick,
  throttle or rudder held off-centre), so they can't be mapped.
  Only A and B are pressure-sensitive (the stick has two pressure channels;
  Launch is an on/off button).

  "Apply & save" uses the results straight away and saves them to hori.ini
  next to the exe; hori.exe reads the same file. "Reset dead zones" in the
  main window goes back to raw axis values (the button signals are kept).

Logging
  Log level    Default: messages, the settings in use and the wizard results.
               Debug:   also the wizard's raw measurements and a table of the
                        most recent raw sensor data.
  Raw lines kept  How many rows of raw sensor data the Debug log keeps (10-5000).
                  A row is added when an axis moves by 2 or more or a button
                  changes, so sensor flicker doesn't fill it up.
  Overwrite the log each run / New log file each run
               Logs go in a "logs" folder next to the exe: hori-test.log, or
               hori-test-<date>-<time>.log for a new file each run.
  The log is written when the wizard finishes, when you click "Save log now",
  and when you close the window. These settings are saved in hori.ini too.


4. Run it
---------
Double-click hori.exe, or run it from a terminal for options.
Leave the window open while you play; Ctrl+C or closing the window stops it.
To check it works: Win+R -> joy.cpl -> "vJoy Device" -> Properties.

To start it automatically: Win+R -> shell:startup, and put a shortcut to
hori.exe there (add options to the shortcut's Target, e.g. hori.exe -m shift).


Mapping
-------
Axes:    Stick -> X, Y    Throttle -> Z    Rudder -> Rz
         Hat -> Rx, Ry    Pressure 1 (Button A) -> Slider
         Pressure 2 (Button B) -> Dial/Slider2
POVs:    D-PAD 1 -> POV 1    D-PAD 2 -> POV 2    D-PAD 3 -> POV 3
         (diagonals work)
Buttons: 1 Trigger   2 Button C   3 Launch   4 Button A   5 Button B
         6 Hat push   7 SW-1   8 Button D   9 Button ST
         10/11/12 M1/M2/M3 (with -m buttons)
         (If vJoy has fewer than 2 POVs, D-PAD 1 is buttons 16-19 and
          D-PAD 2 is buttons 20-23. With fewer than 3, D-PAD 3 is buttons
          14 up, 15 right, 13 left.)

D-pads in PCSX2 (and other games that read controllers through SDL): these
don't see vJoy's POV hats, so the D-pads do nothing there. In the test
window set "D-pads in games" to "Buttons (PCSX2 and SDL games)" (or start
hori.exe with -D buttons), then bind them again in the game:
         D-PAD 1: 16 up, 17 right, 18 down, 19 left
         D-PAD 2: 20 up, 21 right, 22 down, 23 left
         D-PAD 3: 14 up, 15 right, 13 left
The setting is saved, and hori.exe uses it too.

Options:
  -m buttons   (default) the M1/M2/M3 switch holds button 10, 11 or 12
  -m shift     every button number goes up by 32 in M2 and 64 in M3,
               giving 3 separate banks of buttons (needs 87 vJoy buttons)
  -m axis      the switch position is the Wheel axis
  -m off       ignore the switch
  -t N         A/B press threshold, 0-254 (default 20); lower = lighter press
  -n ID        use vJoy device ID instead of 1
  -D buttons   D-pads as buttons 13-23 instead of POV hats (for PCSX2);
               -D hats goes back. Without -D, the test window's setting is used.
  -d           print raw data from the stick (for troubleshooting)


Not supported on Windows (yet)
------------------------------
  - Rumble (the Linux version has it).
  - The dials: they're mechanical and don't send anything over USB.


Troubleshooting
---------------
"Could not load vJoyInterface.dll"  -> vJoy isn't installed (step 1).
"vJoy device 1 doesn't exist"       -> configure it (step 2).
"plugged in but can't be opened"    -> WinUSB isn't installed on it (step 3).
Stuck at "Waiting for the stick..." -> check that it's plugged in, then redo step 3.
A game sees nothing                 -> some games only take Xbox controllers;
                                       tools like Steam Input can remap vJoy.

Credits and licenses
--------------------
This is a fork of mhn_hotas_uinput by Daniel O'Neill (MIT, see LICENSE).
It builds on hfsd (the original Windows driver, by tamanegi.org), libusb
1.0.27 (built into the exes; LGPL 2.1, see LICENSE-libusb.txt), libwdi's
pki.c (built into hori-driver.exe; LGPL 3), vJoy (MIT; its installer is
included in the installer) and NSIS (builds the installer). hori-test.exe's
"About..." button lists them with links; THIRD-PARTY-NOTICES.txt has the
details, and the installer puts their license texts in its "licenses" folder.

Building:
  ./build-windows.sh <libusb-1.0.27 Windows release folder>    (needs mingw-w64)
  ./build-installer.sh <vJoySetup_v2.2.2.0_Win10_Win11.exe>    (needs NSIS)
