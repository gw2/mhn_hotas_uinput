/*
 * About window: this project and the projects it's built on, with their
 * licenses and links. THIRD-PARTY-NOTICES.txt has the same in full.
 */
#include <stdio.h>
#include <string.h>

#include "hori_win.h"

#include <commctrl.h>
#include <shellapi.h>

#define ABOUT_W 780
#define ABOUT_H 840

enum { IDC_ABOUT_NOTICES = 401, IDC_ABOUT_LGPL, IDC_ABOUT_CLOSE, IDC_ABOUT_LINK = 420 };

/* Body text uses SysLink markup for links. Keep in step with THIRD-PARTY-NOTICES.txt. */
static const struct { const wchar_t *title; const wchar_t *body; } about_entries[] = {
	{ L"This driver",
	  L"Windows and Linux driver for the Mitsubishi HORI/Namco Flightstick 2 (Ace Combat 5). "
	  L"MIT license. Source and downloads: "
	  L"<a href=\"https://github.com/gw2/mhn_hotas_uinput\">github.com/gw2/mhn_hotas_uinput</a>" },
	{ L"mhn_hotas_uinput, by Daniel O'Neill",
	  L"The original Linux driver this project is a fork of. MIT license. "
	  L"<a href=\"https://github.com/danieloneill/mhn_hotas_uinput\">GitHub</a>  \x00b7  "
	  L"<a href=\"https://oneill.app\">oneill.app</a>.  No donation link offered." },
	{ L"hfsd, by tamanegi.org",
	  L"The original Windows driver for the Flight Stick 2 (2004). The USB protocol used here comes from it: "
	  L"the report layouts, button bits, pressure scaling and rumble request. Free, no warranty, no restrictions. "
	  L"<a href=\"https://www.tamanegi.org/prog/hfsd/\">tamanegi.org/prog/hfsd</a>.  No donation link offered." },
	{ L"libusb 1.0.27",
	  L"USB access library, built into hori.exe and hori-test.exe. LGPL 2.1 or later; the full text is in "
	  L"LICENSE-libusb.txt. You can rebuild these programs against a modified libusb with build-windows.sh. "
	  L"<a href=\"https://libusb.info\">libusb.info</a>  \x00b7  "
	  L"<a href=\"https://github.com/libusb/libusb/tree/v1.0.27\">source of v1.0.27</a>.  No donation link offered." },
	{ L"vJoy, by Shaul Eizikovich, maintained by Brunner Elektronik AG",
	  L"The virtual joystick games see. MIT license. The installer includes the official vJoy 2.2.2.0 setup; "
	  L"the zip's Setup check downloads it. "
	  L"<a href=\"https://github.com/BrunnerInnovation/vJoy\">GitHub</a>  \x00b7  "
	  L"<a href=\"https://sourceforge.net/projects/vjoystick/\">SourceForge</a>.  No donation link offered." },
	{ L"libwdi and Zadig, by Pete Batard",
	  L"hori-driver.exe installs the stick's WinUSB driver with libwdi's pki.c built in (LGPLv3, unmodified, "
	  L"source in third_party/libwdi), signing the driver package the way Zadig does. "
	  L"<a href=\"https://github.com/pbatard/libwdi\">libwdi on GitHub</a>  \x00b7  "
	  L"<a href=\"https://zadig.akeo.ie\">zadig.akeo.ie</a>.  "
	  L"No donation link: his Rufus site points people who want to give something to the "
	  L"<a href=\"https://www.fsf.org/\">Free Software Foundation</a> instead." },
	{ L"NSIS",
	  L"Builds the installer (zlib license; its LZMA decompressor is CPL 1.0, source at "
	  L"<a href=\"https://nsis.sourceforge.io/Download\">nsis.sourceforge.io</a>).  No donation link offered." },
	{ L"Controller drawing and trademarks",
	  L"The controller drawing was supplied by gw2, based on the labels printed on the controller. "
	  L"HORI, Namco and Ace Combat are trademarks of their owners; this project isn't affiliated with or "
	  L"endorsed by them or by any of the projects above." },
};
#define ABOUT_N (int)(sizeof(about_entries) / sizeof(about_entries[0]))

static HWND about_hwnd;
static int about_title_y[ABOUT_N];
static HBRUSH about_bg;

/* A file shipped next to the exe (notices, licenses). */
static void about_open_file(HWND w, const char *name)
{
	char path[MAX_PATH];
	if( GetModuleFileNameA(NULL, path, sizeof(path)) ) {
		char *slash = strrchr(path, '\\');
		if( slash ) {
			snprintf(slash + 1, sizeof(path) - (slash + 1 - path), "%s", name);
			if( GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ) {
				ShellExecuteA(w, "open", path, NULL, NULL, SW_SHOWNORMAL);
				return;
			}
		}
	}
	char msg[300];
	snprintf(msg, sizeof(msg), "%s isn't next to this program. It's in the download zip and in the source on GitHub.", name);
	MessageBoxA(w, msg, "About", MB_ICONINFORMATION);
}

static void about_create(HWND w)
{
	int y = 84;
	for( int i=0; i < ABOUT_N; i++ ) {
		about_title_y[i] = y;
		y += 24;
		HWND link = CreateWindowExW(0, WC_LINK, about_entries[i].body, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
				20, y, ABOUT_W - 40, 20, w, (HMENU)(INT_PTR)(IDC_ABOUT_LINK + i), GetModuleHandleW(NULL), NULL);
		SendMessageW(link, WM_SETFONT, (WPARAM)gui_font, TRUE);
		int h = (int)SendMessageW(link, LM_GETIDEALHEIGHT, ABOUT_W - 40, 0);
		if( h < 20 )
			h = 60;
		SetWindowPos(link, NULL, 0, 0, ABOUT_W - 40, h, SWP_NOMOVE | SWP_NOZORDER);
		y += h + 16;
	}

	struct { const char *text; int x, w, id; } buttons[] = {
		{ "Third-party notices", 20, 180, IDC_ABOUT_NOTICES },
		{ "libusb license (LGPL)", 210, 190, IDC_ABOUT_LGPL },
		{ "Close", ABOUT_W - 140, 120, IDC_ABOUT_CLOSE },
	};
	for( size_t i=0; i < sizeof(buttons) / sizeof(buttons[0]); i++ ) {
		HWND b = CreateWindowExA(0, "BUTTON", buttons[i].text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				buttons[i].x, ABOUT_H - 54, buttons[i].w, 32, w, (HMENU)(INT_PTR)buttons[i].id, GetModuleHandleA(NULL), NULL);
		SendMessageA(b, WM_SETFONT, (WPARAM)gui_font, TRUE);
	}
}

static void about_paint(HDC dc)
{
	RECT all = { 0, 0, ABOUT_W, ABOUT_H };
	FillRect(dc, &all, about_bg);
	SetBkMode(dc, TRANSPARENT);

	char head[120];
	snprintf(head, sizeof(head), "Flightstick 2 driver %s", HORI_VERSION);
	SelectObject(dc, gui_bold);
	SetTextColor(dc, RGB(30, 30, 30));
	TextOutA(dc, 20, 16, head, (int)strlen(head));
	SelectObject(dc, gui_font);
	SetTextColor(dc, RGB(90, 90, 90));
	const char *intro = "Built on the work of the projects below. Thank you to all of them.";
	TextOutA(dc, 20, 46, intro, (int)strlen(intro));

	SelectObject(dc, gui_bold);
	SetTextColor(dc, RGB(30, 30, 30));
	for( int i=0; i < ABOUT_N; i++ )
		TextOutW(dc, 20, about_title_y[i], about_entries[i].title, (int)wcslen(about_entries[i].title));
	SelectObject(dc, gui_font);
}

static LRESULT CALLBACK about_wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	switch( msg ) {
	case WM_CREATE:
		about_create(w);
		return 0;

	case WM_NOTIFY: {
		NMHDR *n = (NMHDR *)lp;
		if( (NM_CLICK == n->code || NM_RETURN == n->code) && n->idFrom >= IDC_ABOUT_LINK ) {
			NMLINK *l = (NMLINK *)lp;
			if( 0 == wcsncmp(l->item.szUrl, L"https://", 8) )
				ShellExecuteW(w, L"open", l->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
		}
		return 0;
	}

	case WM_CTLCOLORSTATIC:
		SetBkColor((HDC)wp, RGB(250, 250, 250));
		return (LRESULT)about_bg;

	case WM_COMMAND:
		switch( LOWORD(wp) ) {
		case IDC_ABOUT_NOTICES: about_open_file(w, "THIRD-PARTY-NOTICES.txt"); break;
		case IDC_ABOUT_LGPL:    about_open_file(w, "LICENSE-libusb.txt"); break;
		case IDC_ABOUT_CLOSE:   DestroyWindow(w); break;
		}
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(w, &ps);
		about_paint(dc);
		EndPaint(w, &ps);
		return 0;
	}

	case WM_DESTROY:
		about_hwnd = NULL;
		return 0;
	}
	return DefWindowProcW(w, msg, wp, lp);
}

void about_register_class(HINSTANCE hi)
{
	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.hInstance = hi;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIconA(hi, MAKEINTRESOURCEA(1));
	wc.lpfnWndProc = about_wndproc;
	wc.lpszClassName = L"HoriAbout";
	RegisterClassW(&wc);
	about_bg = CreateSolidBrush(RGB(250, 250, 250));
}

void about_open(HWND owner)
{
	if( about_hwnd ) {
		SetForegroundWindow(about_hwnd);
		return;
	}
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
	RECT r = { 0, 0, ABOUT_W, ABOUT_H };
	AdjustWindowRect(&r, style, FALSE);
	about_hwnd = CreateWindowW(L"HoriAbout", L"About", style, CW_USEDEFAULT, CW_USEDEFAULT,
			r.right - r.left, r.bottom - r.top, owner, NULL, GetModuleHandleW(NULL), NULL);
	ShowWindow(about_hwnd, SW_SHOW);
}
