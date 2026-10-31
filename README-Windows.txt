HORI/Namco Flightstick 2 (Ace Combat 5) on Windows 10/11
=========================================================

hori.exe reads the stick and feeds a vJoy virtual joystick that games see
as a normal controller. One-time setup takes about 10 minutes.


Quick setup: the Setup check
----------------------------
Plug in the stick and run hori-test.exe. If anything is missing it opens
the Setup check, which checks three things and offers to fix each one:
  1. vJoy installed      -> downloads vJoy 2.2.2.0 and runs its installer
  2. vJoy device set up  -> runs vJoyConfig: 8 axes, 32 buttons, 3 POV hats
                            (this replaces vJoy device 1's current setup)
  3. Stick has WinUSB    -> downloads Zadig 2.9 and opens it with WinUSB
                            selected; pick USB ID 06D3 0F10, click Install Driver
Nothing is downloaded or installed until you click and say yes, and Windows
asks for admin permission for each install. Downloads come from the official
GitHub releases (vJoy is MIT licensed, Zadig GPLv3) and are checked against
a known SHA-256 fingerprint before they run; files go in
%LOCALAPPDATA%\hori-setup. You can open it any time with "Setup check...".

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
  - Number of buttons: 32  (or 128 if you want "-m shift")
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


Testing: hori-test.exe
----------------------
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
  A, B, the hat push, each D-pad direction, D, ST and M1/M2/M3), and
  records which signal it really sends, including whether it's
  pressure-sensitive.
  D-PAD 3 has three positions: up, right and left (the original driver
  called up "middle"). There is no down.
  Dials: they're mechanical adjusters and send nothing over USB (tested:
  turning them changes no axis, button or data byte, even with the stick,
  throttle or rudder held off-centre), so they can't be mapped.
  If a button doesn't send what the original driver expected, the results
  say so and Apply fixes it. If two buttons send the same signal, it tells
  you straight away.
  About Launch: the stick only has two pressure channels (the original driver
  gave them to A and B). If Launch turns out to use one of them, the wizard
  will show it as pressure-sensitive and use it that way.

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

Options:
  -m buttons   (default) the M1/M2/M3 switch holds button 10, 11 or 12
  -m shift     every button number goes up by 32 in M2 and 64 in M3,
               giving 3 separate banks of buttons (needs 128 vJoy buttons)
  -m axis      the switch position is the Wheel axis
  -m off       ignore the switch
  -t N         A/B press threshold, 0-254 (default 20); lower = lighter press
  -n ID        use vJoy device ID instead of 1
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

Building: ./build-windows.sh <libusb-1.0.27 Windows release folder> (needs mingw-w64)
