/*
 * Setup check: makes sure the two things the stick needs on Windows are in
 * place, and offers to fix them:
 *
 *   1. vJoy is installed           -> download and run the official vJoy installer
 *   2. vJoy device is set up       -> vJoyConfig with the axes/buttons/POVs we use
 *   3. The stick has WinUSB        -> download Zadig and open it ready to install
 *
 * Downloads come from the projects' own GitHub releases and are checked
 * against the SHA-256 recorded below before anything is run. Nothing is
 * downloaded or installed without the user clicking for it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "hori_win.h"

#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <bcrypt.h>

#define SETUP_W 780
#define SETUP_H 560

#define S_OK_COL    RGB(30, 130, 60)
#define S_WARN_COL  RGB(180, 120, 0)
#define S_BAD_COL   RGB(190, 40, 40)
#define S_DIM_COL   RGB(140, 140, 140)
#define S_TEXT_COL  RGB(30, 30, 30)
#define S_BG_COL    RGB(250, 250, 250)

enum {
	IDC_SETUP_ROW1 = 301, IDC_SETUP_ROW2, IDC_SETUP_ROW3, IDC_SETUP_RECHECK, IDC_SETUP_CLOSE,
};

#define WM_SETUP_DONE   (WM_APP + 1)

/* Pinned downloads: updating to a newer vJoy or Zadig means updating these. */
struct setup_download {
	const char *what;
	const char *file;
	const char *url;
	const char *sha256;     /* lowercase hex */
	const char *size;
};

static const struct setup_download dl_vjoy = {
	"vJoy 2.2.2.0",
	"vJoySetup_v2.2.2.0_Win10_Win11.exe",
	"https://github.com/BrunnerInnovation/vJoy/releases/download/v2.2.2.0/vJoySetup_v2.2.2.0_Win10_Win11.exe",
	"ef569a3105cd301b89580f18f60c66b339e95296acf2c0dfcaf4b4bbf8ab68fe",
	"5.3 MB",
};

static const struct setup_download dl_zadig = {
	"Zadig 2.9",
	"zadig-2.9.exe",
	"https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe",
	"4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b",
	"5.3 MB",
};

/* Zadig reads this from its own folder: WinUSB pre-selected, close when done. */
static const char zadig_ini[] =
	"# Written by hori-test.exe for the Flightstick 2\r\n"
	"[general]\r\n"
	"  exit_on_success = true\r\n"
	"[device]\r\n"
	"  list_all = false\r\n"
	"[driver]\r\n"
	"  default_driver = 0\r\n";

#define SETUP_POVS 3

enum { ACT_NONE, ACT_INSTALL_VJOY, ACT_ENABLE_VJOY, ACT_CONFIG_VJOY, ACT_WINUSB, ACT_ZADIG };

/* hori-driver.exe next to this program: one-click WinUSB install (Zadig is the fallback). */
static int driver_tool(char *out, size_t size)
{
	char path[MAX_PATH];
	if( !GetModuleFileNameA(NULL, path, sizeof(path)) )
		return 0;
	char *slash = strrchr(path, '\\');
	if( !slash )
		return 0;
	snprintf(slash + 1, sizeof(path) - (slash + 1 - path), "hori-driver.exe");
	if( GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES )
		return 0;
	if( out )
		snprintf(out, size, "%s", path);
	return 1;
}
enum { ST_OK, ST_WARN, ST_BAD, ST_DIM };

struct setup_row {
	int state;
	const char *title;
	char detail[400];
	const char *button;
	int action;
};

static HWND setup_hwnd;
static volatile LONG setup_busy;
static char setup_status[400];      /* progress line; guarded by hori_lock */
static int setup_status_bad;
static int setup_usb = USBS_NOT_PLUGGED;

static void setup_say(int bad, const char *fmt, ...)
{
	char buf[400];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	EnterCriticalSection(&hori_lock);
	snprintf(setup_status, sizeof(setup_status), "%s", buf);
	setup_status_bad = bad;
	LeaveCriticalSection(&hori_lock);
	hori_log("Setup check: %s\n", buf);
}

/* ---- checks ---- */

static void vjoy_path(char *out, size_t size, const char *file)
{
	const char *pf = getenv("ProgramFiles");
	snprintf(out, size, "%s\\vJoy\\x64\\%s", pf ? pf : "C:\\Program Files", file);
}

static void setup_rows(struct setup_row rows[3])
{
	int need_buttons = hori_vjoy_buttons_needed() > 32 ? 128 : 32;

	EnterCriticalSection(&hori_lock);
	int state = vj_state, buttons = vj_buttons, povs = vj_cont_povs + vj_disc_povs;
	int axes = hori_vjoy_axes_ok();
	char err[200];
	snprintf(err, sizeof(err), "%s", vj_error);
	LeaveCriticalSection(&hori_lock);

	/* 1. installed */
	rows[0].title = "1. vJoy (the virtual joystick games see)";
	rows[0].action = ACT_NONE;
	rows[0].button = NULL;
	if( VJS_NOT_INSTALLED == state ) {
		rows[0].state = ST_BAD;
		snprintf(rows[0].detail, sizeof(rows[0].detail),
				"vJoy isn't installed. Downloads %s (%s) from its official GitHub release, "
				"checks it, and runs its installer.", dl_vjoy.what, dl_vjoy.size);
		rows[0].button = "Download and install vJoy";
		rows[0].action = ACT_INSTALL_VJOY;
	} else {
		rows[0].state = ST_OK;
		snprintf(rows[0].detail, sizeof(rows[0].detail), "vJoy is installed.");
	}

	/* 2. device set up */
	rows[1].title = "2. vJoy device set up for the stick";
	rows[1].action = ACT_NONE;
	rows[1].button = NULL;
	switch( state ) {
	case VJS_NOT_INSTALLED:
		rows[1].state = ST_DIM;
		snprintf(rows[1].detail, sizeof(rows[1].detail), "Needs vJoy first.");
		break;
	case VJS_DISABLED:
		rows[1].state = ST_BAD;
		snprintf(rows[1].detail, sizeof(rows[1].detail), "vJoy is turned off.");
		rows[1].button = "Turn vJoy on";
		rows[1].action = ACT_ENABLE_VJOY;
		break;
	case VJS_BUSY:
		rows[1].state = ST_WARN;
		snprintf(rows[1].detail, sizeof(rows[1].detail),
				"vJoy device %u is in use by another program (another hori.exe or hori-test.exe?). "
				"Close it, then click Check again.", opt_vjoy_id);
		break;
	case VJS_OK:
		if( axes && buttons >= need_buttons && povs >= SETUP_POVS ) {
			rows[1].state = ST_OK;
			snprintf(rows[1].detail, sizeof(rows[1].detail),
					"Device %u: 8 axes, %d buttons, %d POV hats.", opt_vjoy_id, buttons, povs);
			break;
		}
		rows[1].state = ST_WARN;
		snprintf(rows[1].detail, sizeof(rows[1].detail),
				"Device %u has %s%d buttons and %d POV hats; the stick needs 8 axes, %d buttons and %d POV hats.",
				opt_vjoy_id, axes ? "" : "missing axes, ", buttons, povs, need_buttons, SETUP_POVS);
		rows[1].button = "Set up vJoy device";
		rows[1].action = ACT_CONFIG_VJOY;
		break;
	default:
		rows[1].state = ST_BAD;
		snprintf(rows[1].detail, sizeof(rows[1].detail), "%s", err[0] ? err : "vJoy device isn't usable.");
		rows[1].button = "Set up vJoy device";
		rows[1].action = ACT_CONFIG_VJOY;
		break;
	}

	/* 3. WinUSB */
	rows[2].title = "3. The stick's USB driver (WinUSB)";
	rows[2].action = ACT_NONE;
	rows[2].button = NULL;
	switch( setup_usb ) {
	case USBS_CONNECTED:
		rows[2].state = ST_OK;
		snprintf(rows[2].detail, sizeof(rows[2].detail), "The stick is connected and working.");
		break;
	case USBS_CONNECTING:
		rows[2].state = ST_OK;
		snprintf(rows[2].detail, sizeof(rows[2].detail), "The stick has its driver; connecting...");
		break;
	case USBS_NO_WINUSB:
		rows[2].state = ST_BAD;
		rows[2].button = "Install the stick's driver";
		if( driver_tool(NULL, 0) ) {
			snprintf(rows[2].detail, sizeof(rows[2].detail),
					"The stick is plugged in but has no WinUSB driver. One click installs it with "
					"hori-driver.exe; Windows asks for admin permission.");
			rows[2].action = ACT_WINUSB;
		} else {
			snprintf(rows[2].detail, sizeof(rows[2].detail),
					"The stick is plugged in but has no WinUSB driver. Downloads %s (%s), checks it, "
					"and opens it with WinUSB selected: pick USB ID 06D3 0F10 and click Install Driver.",
					dl_zadig.what, dl_zadig.size);
			rows[2].action = ACT_ZADIG;
		}
		break;
	default:
		rows[2].state = ST_DIM;
		snprintf(rows[2].detail, sizeof(rows[2].detail),
				"The stick isn't plugged in. Plug it in to check its driver.");
		break;
	}
}

/* Everything the stick needs is in place (an unplugged stick doesn't count against it). */
int setup_all_ok(void)
{
	struct setup_row rows[3];
	setup_usb = hori_usb_state();
	setup_rows(rows);
	return rows[0].state == ST_OK && rows[1].state == ST_OK && rows[2].state != ST_BAD;
}

/* ---- downloads ---- */

static int sha256_file(const char *path, char hex[65])
{
	int ok = 0;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if( INVALID_HANDLE_VALUE == f )
		return 0;

	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	UCHAR digest[32], buf[65536];
	DWORD got;
	if( BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 )
		goto out;
	if( BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) < 0 )
		goto out;
	while( ReadFile(f, buf, sizeof(buf), &got, NULL) && got )
		if( BCryptHashData(hash, buf, got, 0) < 0 )
			goto out;
	if( BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0 )
		goto out;
	for( int i=0; i < 32; i++ )
		snprintf(hex + i * 2, 3, "%02x", digest[i]);
	ok = 1;
out:
	if( hash ) BCryptDestroyHash(hash);
	if( alg ) BCryptCloseAlgorithmProvider(alg, 0);
	CloseHandle(f);
	return ok;
}

/* Our own download folder, %LOCALAPPDATA%\hori-setup[\sub]. */
static int setup_dir(char *out, size_t size, const char *sub)
{
	char base[MAX_PATH];
	if( S_OK != SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base) )
		return 0;
	snprintf(out, size, "%s\\hori-setup", base);
	CreateDirectoryA(out, NULL);
	if( sub ) {
		size_t len = strlen(out);
		snprintf(out + len, size - len, "\\%s", sub);
		CreateDirectoryA(out, NULL);
	}
	return 1;
}

/* Download (or reuse) a pinned file into dir and check its SHA-256. */
static int setup_fetch(const struct setup_download *dl, const char *dir, char *path, size_t size)
{
	char hex[65];
	snprintf(path, size, "%s\\%s", dir, dl->file);

	if( sha256_file(path, hex) && 0 == strcmp(hex, dl->sha256) ) {
		setup_say(0, "Using the %s already downloaded (fingerprint checked).", dl->what);
		return 1;
	}

	setup_say(0, "Downloading %s (%s) from GitHub...", dl->what, dl->size);
	DeleteFileA(path);
	HRESULT hr = URLDownloadToFileA(NULL, dl->url, path, 0, NULL);
	if( S_OK != hr ) {
		setup_say(1, "Downloading %s failed (error 0x%08lx). Check your internet connection.", dl->what, (unsigned long)hr);
		return 0;
	}
	if( !sha256_file(path, hex) || strcmp(hex, dl->sha256) ) {
		DeleteFileA(path);
		setup_say(1, "The downloaded %s didn't match its known fingerprint, so it was deleted and not run.", dl->what);
		return 0;
	}
	setup_say(0, "Downloaded %s; fingerprint matches.", dl->what);
	return 1;
}

/* Run a program as admin (Windows asks), wait for it, return its exit code or -1. */
static long setup_run_admin(const char *exe, const char *params, const char *dir, int show)
{
	SHELLEXECUTEINFOA sei;
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = "runas";
	sei.lpFile = exe;
	sei.lpParameters = params;
	sei.lpDirectory = dir;
	sei.nShow = show;
	if( !ShellExecuteExA(&sei) || !sei.hProcess ) {
		if( ERROR_CANCELLED == GetLastError() )
			setup_say(1, "Cancelled at the Windows admin prompt.");
		else
			setup_say(1, "Couldn't start %s (error %lu).", exe, (unsigned long)GetLastError());
		return -1;
	}
	WaitForSingleObject(sei.hProcess, INFINITE);
	DWORD code = 0;
	GetExitCodeProcess(sei.hProcess, &code);
	CloseHandle(sei.hProcess);
	return (long)code;
}

/* ---- actions (run on their own thread) ---- */

static void act_install_vjoy(void)
{
	char dir[MAX_PATH], path[MAX_PATH];
	if( !setup_dir(dir, sizeof(dir), NULL) || !setup_fetch(&dl_vjoy, dir, path, sizeof(path)) )
		return;

	setup_say(0, "Installing vJoy: approve the Windows prompt, then wait for the installer to finish...");
	hori_vjoy_release();
	long code = setup_run_admin(path, "/SILENT /SUPPRESSMSGBOXES /NORESTART", dir, SW_SHOWNORMAL);
	hori_vjoy_reinit();
	if( code < 0 )
		return;
	if( VJS_NOT_INSTALLED == vj_state )
		setup_say(1, "The vJoy installer finished (code %ld) but vJoy still isn't found. A restart may be needed.", code);
	else
		setup_say(0, "vJoy is installed.");
}

static void act_vjoyconfig(const char *params, const char *what)
{
	char exe[MAX_PATH];
	vjoy_path(exe, sizeof(exe), "vJoyConfig.exe");
	if( GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES ) {
		setup_say(1, "vJoyConfig.exe wasn't found in vJoy's folder; reinstall vJoy.");
		return;
	}

	setup_say(0, "%s: approve the Windows prompt...", what);
	hori_vjoy_release();
	long code = setup_run_admin(exe, params, NULL, SW_HIDE);

	// The vJoy driver restarts after a change; give it a moment to come back.
	for( int i=0; i < 5; i++ ) {
		Sleep(1000);
		hori_vjoy_reinit();
		if( VJS_OK == vj_state )
			break;
	}
	if( code < 0 )
		return;
	if( VJS_OK == vj_state )
		setup_say(0, "%s: done.", what);
	else
		setup_say(1, "%s: vJoyConfig finished (code %ld) but vJoy isn't ready yet. Click Check again in a moment.", what, code);
}

/* Wait briefly for the stick to come back with a driver after an install. */
static void wait_for_winusb(void)
{
	for( int i=0; i < 10; i++ ) {
		setup_usb = hori_usb_state();
		if( USBS_NO_WINUSB != setup_usb )
			break;
		Sleep(500);
	}
}

static void act_winusb(void)
{
	char exe[MAX_PATH];
	if( !driver_tool(exe, sizeof(exe)) ) {
		setup_say(1, "hori-driver.exe isn't next to this program.");
		return;
	}
	setup_say(0, "Installing the stick's WinUSB driver: approve the Windows prompt...");
	long code = setup_run_admin(exe, "install", NULL, SW_HIDE);
	if( code < 0 )
		return;
	wait_for_winusb();
	if( 0 == code && USBS_NO_WINUSB != setup_usb )
		setup_say(0, "The stick's WinUSB driver is installed.");
	else if( 0 == code )
		setup_say(1, "hori-driver.exe finished, but the stick still has no WinUSB driver. Unplug it and plug it back in.");
	else
		setup_say(1, "hori-driver.exe failed (code %ld). Run \"hori-driver.exe install\" from a terminal to see why.", code);
}

static void act_zadig(void)
{
	char dir[MAX_PATH], path[MAX_PATH], ini[MAX_PATH];
	if( !setup_dir(dir, sizeof(dir), "zadig") || !setup_fetch(&dl_zadig, dir, path, sizeof(path)) )
		return;

	snprintf(ini, sizeof(ini), "%s\\zadig.ini", dir);
	FILE *f = fopen(ini, "wb");
	if( f ) {
		fwrite(zadig_ini, 1, sizeof(zadig_ini) - 1, f);
		fclose(f);
	}

	setup_say(0, "Zadig is open: pick the device with USB ID 06D3 0F10, check WinUSB is selected, click Install Driver.");
	long code = setup_run_admin(path, NULL, dir, SW_SHOWNORMAL);
	if( code < 0 )
		return;

	wait_for_winusb();
	if( USBS_NO_WINUSB == setup_usb )
		setup_say(1, "Zadig closed, but the stick still has no WinUSB driver. Try again, and check the USB ID was 06D3 0F10.");
	else
		setup_say(0, "The stick's WinUSB driver is installed.");
}

static DWORD WINAPI setup_thread(LPVOID param)
{
	char params[160];
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	switch( (int)(INT_PTR)param ) {
	case ACT_INSTALL_VJOY:
		act_install_vjoy();
		break;
	case ACT_ENABLE_VJOY:
		act_vjoyconfig("enable on", "Turning vJoy on");
		break;
	case ACT_CONFIG_VJOY:
		snprintf(params, sizeof(params), "%u -f -a x y z rx ry rz sl0 sl1 -b %d -p %d",
				opt_vjoy_id, hori_vjoy_buttons_needed() > 32 ? 128 : 32, SETUP_POVS);
		act_vjoyconfig(params, "Setting up vJoy device");
		break;
	case ACT_WINUSB:
		act_winusb();
		break;
	case ACT_ZADIG:
		act_zadig();
		break;
	}

	CoUninitialize();
	InterlockedExchange(&setup_busy, 0);
	if( setup_hwnd )
		PostMessageA(setup_hwnd, WM_SETUP_DONE, 0, 0);
	return 0;
}

/* Explain what's about to happen and ask first. */
static int setup_confirm(HWND w, int action)
{
	char msg[1100];
	int buttons = hori_vjoy_buttons_needed() > 32 ? 128 : 32;

	switch( action ) {
	case ACT_INSTALL_VJOY:
		snprintf(msg, sizeof(msg),
				"Download %s (%s) from github.com/BrunnerInnovation/vJoy and install it?\n\n"
				"The download is checked against its known SHA-256 fingerprint before it runs. "
				"Windows will ask for admin permission. vJoy is MIT licensed.",
				dl_vjoy.what, dl_vjoy.size);
		break;
	case ACT_ENABLE_VJOY:
		snprintf(msg, sizeof(msg), "Turn vJoy on? Windows will ask for admin permission.");
		break;
	case ACT_CONFIG_VJOY:
		snprintf(msg, sizeof(msg),
				"Set up vJoy device %u with 8 axes, %d buttons and %d POV hats?\n\n"
				"This replaces device %u's current setup, so any other program using that device "
				"will see the change. Windows will ask for admin permission.",
				opt_vjoy_id, buttons, SETUP_POVS, opt_vjoy_id);
		break;
	case ACT_WINUSB:
		snprintf(msg, sizeof(msg),
				"Install the WinUSB driver for the stick (USB ID 06D3 0F10)?\n\n"
				"hori-driver.exe adds a small driver package that points the stick at the WinUSB driver "
				"built into Windows. Like Zadig, it signs the package with a certificate made just for it, "
				"adds that certificate to Windows' trusted certificates, and deletes the certificate's private "
				"key so it can't sign anything else. Windows will ask for admin permission.\n\n"
				"To undo it later: run \"hori-driver.exe uninstall\" as admin, or uninstall the app.");
		break;
	case ACT_ZADIG:
		snprintf(msg, sizeof(msg),
				"Download %s (%s) from github.com/pbatard/libwdi and open it?\n\n"
				"The download is checked against its known SHA-256 fingerprint before it runs. "
				"Zadig opens with WinUSB selected. In Zadig:\n"
				"  1. Pick the device whose USB ID shows 06D3 0F10 (it may be called \"Unknown Device\").\n"
				"  2. Click Install Driver.\n"
				"Zadig closes when it's done.\n\n"
				"Note: to install the driver, Zadig creates its own signing certificate and adds it to "
				"Windows' trusted certificates. Zadig is GPLv3 licensed.",
				dl_zadig.what, dl_zadig.size);
		break;
	default:
		return 0;
	}
	return IDYES == MessageBoxA(w, msg, "Setup check", MB_YESNO | MB_ICONQUESTION);
}

/* ---- window ---- */

static void setup_update_buttons(void)
{
	struct setup_row rows[3];
	setup_rows(rows);
	int busy = setup_busy != 0;
	for( int i=0; i < 3; i++ ) {
		HWND b = GetDlgItem(setup_hwnd, IDC_SETUP_ROW1 + i);
		if( rows[i].button ) {
			SetWindowTextA(b, rows[i].button);
			ShowWindow(b, SW_SHOW);
			EnableWindow(b, !busy);
		} else
			ShowWindow(b, SW_HIDE);
	}
	EnableWindow(GetDlgItem(setup_hwnd, IDC_SETUP_RECHECK), !busy);
}

static void setup_text(HDC dc, int x, int y, COLORREF col, const char *text)
{
	SetTextColor(dc, col);
	TextOutA(dc, x, y, text, (int)strlen(text));
}

static void setup_wrap(HDC dc, int x, int y, int w, int h, COLORREF col, const char *text)
{
	RECT r = { x, y, x + w, y + h };
	SetTextColor(dc, col);
	DrawTextA(dc, text, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
}

static void setup_paint(HDC dc)
{
	static const COLORREF dot[] = { S_OK_COL, S_WARN_COL, S_BAD_COL, S_DIM_COL };
	static const char *const mark[] = { "OK", "!", "X", "-" };
	struct setup_row rows[3];
	char status[400];
	int bad;

	setup_rows(rows);
	EnterCriticalSection(&hori_lock);
	snprintf(status, sizeof(status), "%s", setup_status);
	bad = setup_status_bad;
	LeaveCriticalSection(&hori_lock);

	RECT all = { 0, 0, SETUP_W, SETUP_H };
	HBRUSH bg = CreateSolidBrush(S_BG_COL);
	FillRect(dc, &all, bg);
	DeleteObject(bg);
	SetBkMode(dc, TRANSPARENT);

	SelectObject(dc, gui_bold);
	setup_text(dc, 20, 16, S_TEXT_COL, "Setup check");
	SelectObject(dc, gui_font);
	setup_wrap(dc, 20, 46, SETUP_W - 40, 44, S_TEXT_COL,
			"The stick needs vJoy and a WinUSB driver on Windows. This checks both and can set them up. "
			"Nothing is downloaded or installed until you click a button and say yes.");

	for( int i=0; i < 3; i++ ) {
		int y = 100 + i * 120;
		HBRUSH b = CreateSolidBrush(dot[rows[i].state]);
		HPEN p = (HPEN)GetStockObject(NULL_PEN);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
		Ellipse(dc, 20, y + 2, 44, y + 26);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		SelectObject(dc, gui_small);
		SIZE sz;
		GetTextExtentPoint32A(dc, mark[rows[i].state], (int)strlen(mark[rows[i].state]), &sz);
		setup_text(dc, 32 - sz.cx / 2, y + 7, RGB(255, 255, 255), mark[rows[i].state]);

		SelectObject(dc, gui_bold);
		setup_text(dc, 56, y + 3, S_TEXT_COL, rows[i].title);
		SelectObject(dc, gui_font);
		setup_wrap(dc, 56, y + 30, rows[i].button ? SETUP_W - 330 : SETUP_W - 80, 80,
				ST_DIM == rows[i].state ? S_DIM_COL : S_TEXT_COL, rows[i].detail);
	}

	int y = 100 + 3 * 120;
	if( status[0] )
		setup_wrap(dc, 20, y, SETUP_W - 40, 40, bad ? S_BAD_COL : S_OK_COL, status);
	SelectObject(dc, gui_small);
	setup_text(dc, 20, SETUP_H - 26, S_DIM_COL,
			"Downloads: vJoy (MIT) and Zadig (GPLv3), from their official GitHub releases, checked by SHA-256.");
	SelectObject(dc, gui_font);
}

static HWND setup_button(HWND parent, const char *text, int x, int y, int w, int id)
{
	HWND c = CreateWindowExA(0, "BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
			x, y, w, 34, parent, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
	SendMessageA(c, WM_SETFONT, (WPARAM)gui_font, TRUE);
	return c;
}

static LRESULT CALLBACK setup_wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	switch( msg ) {
	case WM_CREATE:
		for( int i=0; i < 3; i++ )
			setup_button(w, "", SETUP_W - 260, 100 + i * 120 + 26, 240, IDC_SETUP_ROW1 + i);
		setup_button(w, "Check again", SETUP_W - 330, SETUP_H - 80, 140, IDC_SETUP_RECHECK);
		setup_button(w, "Close", SETUP_W - 170, SETUP_H - 80, 150, IDC_SETUP_CLOSE);
		SetTimer(w, 1, 1000, NULL);
		return 0;

	case WM_TIMER:
		if( !setup_busy )
			setup_usb = hori_usb_state();
		setup_update_buttons();
		InvalidateRect(w, NULL, FALSE);
		return 0;

	case WM_SETUP_DONE:
		setup_usb = hori_usb_state();
		setup_update_buttons();
		InvalidateRect(w, NULL, FALSE);
		return 0;

	case WM_COMMAND: {
		int id = LOWORD(wp);
		if( id >= IDC_SETUP_ROW1 && id <= IDC_SETUP_ROW3 ) {
			struct setup_row rows[3];
			setup_rows(rows);
			int action = rows[id - IDC_SETUP_ROW1].action;
			if( setup_busy || ACT_NONE == action || !setup_confirm(w, action) )
				return 0;
			InterlockedExchange(&setup_busy, 1);
			setup_update_buttons();
			HANDLE t = CreateThread(NULL, 0, setup_thread, (LPVOID)(INT_PTR)action, 0, NULL);
			if( t )
				CloseHandle(t);
			else
				InterlockedExchange(&setup_busy, 0);
		} else if( IDC_SETUP_RECHECK == id ) {
			hori_vjoy_reinit();
			setup_usb = hori_usb_state();
			setup_say(0, "Checked again.");
			setup_update_buttons();
			InvalidateRect(w, NULL, FALSE);
		} else if( IDC_SETUP_CLOSE == id )
			SendMessageA(w, WM_CLOSE, 0, 0);
		return 0;
	}

	case WM_CLOSE:
		if( setup_busy && IDYES != MessageBoxA(w, "Something is still being set up. Close this window anyway? "
				"(It carries on in the background.)", "Setup check", MB_YESNO | MB_ICONQUESTION) )
			return 0;
		DestroyWindow(w);
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(w, &ps);
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP bmp = CreateCompatibleBitmap(dc, SETUP_W, SETUP_H);
		HGDIOBJ old = SelectObject(mem, bmp);
		SelectObject(mem, gui_font);
		setup_paint(mem);
		BitBlt(dc, 0, 0, SETUP_W, SETUP_H, mem, 0, 0, SRCCOPY);
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(w, &ps);
		return 0;
	}

	case WM_DESTROY:
		setup_hwnd = NULL;
		return 0;
	}
	return DefWindowProcA(w, msg, wp, lp);
}

void setup_register_class(HINSTANCE hi)
{
	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.hInstance = hi;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.lpfnWndProc = setup_wndproc;
	wc.lpszClassName = "HoriSetup";
	RegisterClassA(&wc);
}

void setup_open(HWND owner)
{
	if( setup_hwnd ) {
		SetForegroundWindow(setup_hwnd);
		return;
	}
	setup_usb = hori_usb_state();

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
	RECT r = { 0, 0, SETUP_W, SETUP_H };
	AdjustWindowRect(&r, style, FALSE);
	setup_hwnd = CreateWindowA("HoriSetup", "Setup check", style, CW_USEDEFAULT, CW_USEDEFAULT,
			r.right - r.left, r.bottom - r.top, owner, NULL, GetModuleHandleA(NULL), NULL);
	setup_update_buttons();
	ShowWindow(setup_hwnd, SW_SHOW);
}
