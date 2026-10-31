libwdi (vendored subset)
========================

Source:   https://github.com/pbatard/libwdi, tag v1.5.1
          (commit 9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602), the release
          Zadig 2.9 is built from.
Author:   Pete Batard (Akeo Consulting)
License:  GNU LGPL version 3 or later (COPYING-LGPL), which builds on the
          GNU GPL version 3 (COPYING).

Files here, unmodified: pki.c (driver catalog creation and self-signing),
and the headers it needs: libwdi.h, installer.h, logging.h, stdfn.h,
msapi_utf8.h, mssign32.h.

hori-driver.exe is built from ../../hori_driver.c, pki.c, and
../libwdi_shim/config.h (a stand-in for the config.h libwdi's configure
generates). hori_driver.c also provides the few functions pki.c normally
gets from the rest of libwdi (logging, error strings, Windows version).
To use a modified libwdi, replace pki.c and rebuild with build-windows.sh.
