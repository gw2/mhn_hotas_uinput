/*
 * Test window, controller drawing, log file and setup wizard for the Windows driver.
 *
 * The worker thread in hori_win.c calls gui_sample_hook() after every poll,
 * with hori_lock held; that feeds the raw-data log and the wizard. Everything
 * here that the hook touches is only read or written under hori_lock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "hori_win.h"

#include <commctrl.h>
#include <shellapi.h>

#define GUI_W 1282
#define GUI_H 860

#define GUI_OK      RGB(30, 130, 60)
#define GUI_BAD     RGB(190, 40, 40)
#define GUI_WARN    RGB(180, 120, 0)
#define GUI_TEXT    RGB(30, 30, 30)
#define GUI_DIM     RGB(140, 140, 140)
#define GUI_LIT     RGB(60, 170, 90)
#define GUI_SWEPT   RGB(190, 228, 200)
#define GUI_PRESS   RGB(150, 215, 170)
#define GUI_PRESS2  RGB(25, 110, 50)
#define GUI_CENTER  RGB(60, 110, 210)
#define GUI_OFF     RGB(228, 228, 228)
#define GUI_BORDER  RGB(190, 190, 190)
#define GUI_BG      RGB(250, 250, 250)
#define GUI_WHITE   RGB(255, 255, 255)
#define GUI_LINE    RGB(80, 80, 80)
#define GUI_HL      RGB(255, 140, 0)
#define GUI_LED_ON  RGB(255, 25, 45)
#define GUI_LED_OFF RGB(125, 15, 35)
#define GUI_LED_NA  RGB(150, 150, 150)

enum {
	IDC_LEVEL = 101, IDC_RAWLINES, IDC_RAWSPIN, IDC_LOGMODE, IDC_SAVELOG, IDC_OPENLOGS,
	IDC_WIZARD, IDC_RESETCAL, IDC_SETUP, IDC_ABOUT,
	IDC_WIZ_SKIP = 201, IDC_WIZ_NEXT, IDC_WIZ_CANCEL, IDC_WIZ_ALL, IDC_WIZ_ANALOG, IDC_WIZ_BUTTONS,
	IDC_WIZ_BACK, IDC_WIZ_SKIPBTN,
};

/* Things on the drawing that aren't buttons; buttons use their HB_ index. */
enum {
	CTL_MODE = HB_COUNT, CTL_STICK, CTL_HAT, CTL_RUDDER, CTL_THROTTLE,
};

HFONT gui_font, gui_bold, gui_small;
static HFONT gui_big;
static HWND gui_hwnd, gui_rawlines, gui_rawspin;

/* ---- small helpers ---- */

/* Growable text buffer */
struct gbuf {
	char *p;
	size_t len, cap;
};

static void gbuf_printf(struct gbuf *b, const char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if( n < 0 )
		return;
	if( n >= (int)sizeof(tmp) )
		n = sizeof(tmp) - 1;
	if( b->len + n + 1 > b->cap ) {
		size_t cap = b->cap ? b->cap * 2 : 4096;
		while( cap < b->len + n + 1 )
			cap *= 2;
		char *p = realloc(b->p, cap);
		if( !p )
			return;
		b->p = p;
		b->cap = cap;
	}
	memcpy(b->p + b->len, tmp, n + 1);
	b->len += n;
}

/* Append to a fixed buffer, separating items with ", ". */
static void list_add(char *out, size_t size, const char *fmt, ...)
{
	char tmp[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	size_t len = strlen(out);
	snprintf(out + len, size - len, "%s%s", len ? ", " : "", tmp);
}

static void gui_text(HDC dc, int x, int y, COLORREF col, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	SetTextColor(dc, col);
	TextOutA(dc, x, y, buf, (int)strlen(buf));
}

static void gui_wrap(HDC dc, int x, int y, int w, int h, COLORREF col, const char *text)
{
	RECT r = { x, y, x + w, y + h };
	SetTextColor(dc, col);
	DrawTextA(dc, text, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
}

static void gui_heading(HDC dc, int x, int y, const char *text)
{
	SelectObject(dc, gui_bold);
	gui_text(dc, x, y, GUI_TEXT, "%s", text);
	SelectObject(dc, gui_font);
}

static HWND gui_control(HWND parent, DWORD exstyle, const char *cls, const char *text, DWORD style,
		int x, int y, int w, int h, int id)
{
	HWND c = CreateWindowExA(exstyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
			parent, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
	SendMessageA(c, WM_SETFONT, (WPARAM)gui_font, TRUE);
	return c;
}

/* ---- drawing primitives ---- */

static void d_shape(HDC dc, int kind, int x, int y, int w, int h, int rad,
		COLORREF fill, int filled, COLORREF line, int width)
{
	HBRUSH b = filled ? CreateSolidBrush(fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
	HPEN p = width ? CreatePen(PS_SOLID, width, line) : (HPEN)GetStockObject(NULL_PEN);
	HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
	if( 0 == kind )
		Ellipse(dc, x, y, x + w + 1, y + h + 1);
	else
		RoundRect(dc, x, y, x + w + 1, y + h + 1, rad, rad);
	SelectObject(dc, ob);
	SelectObject(dc, op);
	if( filled )
		DeleteObject(b);
	if( width )
		DeleteObject(p);
}

static void d_circle(HDC dc, int cx, int cy, int r, COLORREF fill, int filled, COLORREF line, int width)
{
	d_shape(dc, 0, cx - r, cy - r, 2 * r, 2 * r, 0, fill, filled, line, width);
}

static void d_rrect(HDC dc, int x, int y, int w, int h, int rad, COLORREF fill, int filled, COLORREF line, int width)
{
	d_shape(dc, 1, x, y, w, h, rad, fill, filled, line, width);
}

static void d_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int width)
{
	HPEN p = CreatePen(PS_SOLID, width, col);
	HGDIOBJ op = SelectObject(dc, p);
	MoveToEx(dc, x1, y1, NULL);
	LineTo(dc, x2, y2);
	SelectObject(dc, op);
	DeleteObject(p);
}

/* Pulsing orange outline around whatever the wizard is asking for. */
static int d_pulse(void)
{
	DWORD t = GetTickCount() % 1000;
	return t < 500 ? (int)t / 100 : (int)(1000 - t) / 100;
}

/* ---- the controller drawing ----
 *
 * The line art is a 1536x1024 drawing, stored at 3/4 size as a bitmap resource.
 * Positions below are in full-size drawing units; art_view maps them to the screen. */

#define ART_W       1536
#define ART_H       1024
#define ART_RES_ID  101

static HBITMAP art_bmp;
static HDC art_dc;

enum { V_ALL, V_STICK_TOP, V_STICK_SIDE, V_THR_SIDE, V_THR_TOP, V_COUNT };
static const RECT art_views[V_COUNT] = {
	{ 0, 0, ART_W, ART_H },
	{ 860, 95, 1530, 385 },     /* stick, from the top */
	{ 880, 430, 1470, 880 },    /* stick, from the side (plus our stick X/Y box) */
	{ 20, 0, 800, 345 },        /* throttle, from the side */
	{ 60, 360, 810, 1020 },     /* throttle from the top, and the mode LEDs */
};

enum { G_CIRCLE, G_RECT };
struct art_geo {
	int id, kind;
	int x, y, w, h;     /* circle: centre and radius (w); rect: corner and size */
};

static const struct art_geo art_geo[] = {
	{ HB_DPAD1_TOP,     G_RECT,    922, 164, 10, 22 },
	{ HB_DPAD1_RIGHT,   G_RECT,    934, 191, 24, 10 },
	{ HB_DPAD1_BOTTOM,  G_RECT,    922, 208, 10, 23 },
	{ HB_DPAD1_LEFT,    G_RECT,    891, 191, 25, 10 },
	{ HB_HAT,           G_CIRCLE, 1050, 178, 29, 0 },
	{ HB_A,             G_CIRCLE,  979, 250, 15, 0 },
	{ HB_B,             G_CIRCLE, 1063, 253, 15, 0 },
	{ HB_LAUNCH,        G_CIRCLE, 1024, 290, 16, 0 },
	{ HB_FIRE_C,        G_CIRCLE, 1094, 521, 10, 0 },
	{ HB_TRIGGER,       G_RECT,   1032, 548, 34, 46 },
	{ HB_SW1,           G_RECT,   1030, 672, 40, 50 },
	{ HB_DPAD3_TOP,     G_CIRCLE,  477, 117,  7, 0 },
	{ HB_DPAD3_RIGHT,   G_CIRCLE,  491, 131,  7, 0 },
	{ HB_DPAD3_LEFT,    G_CIRCLE,  463, 131,  7, 0 },
	{ HB_D,             G_CIRCLE,  630, 875, 20, 0 },
	{ HB_ST,            G_CIRCLE,  475, 922,  9, 0 },
	{ HB_DPAD2_TOP,     G_CIRCLE,  532, 845,  7, 0 },
	{ HB_DPAD2_RIGHT,   G_CIRCLE,  554, 855,  7, 0 },
	{ HB_DPAD2_BOTTOM,  G_CIRCLE,  532, 866,  7, 0 },
	{ HB_DPAD2_LEFT,    G_CIRCLE,  510, 855,  7, 0 },
	{ CTL_MODE,         G_RECT,    515, 907, 32, 32 },
	{ CTL_MODE,         G_RECT,    590, 440, 80, 235 },
	{ CTL_HAT,          G_CIRCLE, 1050, 178, 54, 0 },
	{ CTL_RUDDER,       G_RECT,    485, 158, 165, 42 },
	{ CTL_THROTTLE,     G_RECT,    480, 252, 268, 82 },
	{ CTL_STICK,        G_RECT,   1300, 700, 150, 150 },
};
#define ART_GEO_N (int)(sizeof(art_geo) / sizeof(art_geo[0]))

/* The mode LEDs' insides, M1 to M3 */
static const RECT art_leds[3] = {
	{ 604, 452, 657, 477 }, { 604, 545, 657, 570 }, { 604, 637, 657, 662 },
};

/* Which view shows a control best (for the wizard). */
static int ctl_art_view(int id)
{
	switch( id ) {
	case HB_DPAD1_TOP: case HB_DPAD1_RIGHT: case HB_DPAD1_BOTTOM: case HB_DPAD1_LEFT:
	case HB_HAT: case HB_A: case HB_B: case HB_LAUNCH: case CTL_HAT:
		return V_STICK_TOP;
	case HB_FIRE_C: case HB_TRIGGER: case HB_SW1: case CTL_STICK:
		return V_STICK_SIDE;
	case HB_DPAD3_TOP: case HB_DPAD3_RIGHT: case HB_DPAD3_LEFT:
	case CTL_RUDDER: case CTL_THROTTLE:
		return V_THR_SIDE;
	case HB_D: case HB_ST: case HB_DPAD2_TOP: case HB_DPAD2_RIGHT: case HB_DPAD2_BOTTOM: case HB_DPAD2_LEFT:
	case CTL_MODE:
		return V_THR_TOP;
	}
	return V_ALL;
}

struct art_view {
	int sx, sy, sw, sh;     /* part of the drawing shown */
	int dx, dy, dw, dh;     /* where on screen */
	double k;
};

static void art_fit(struct art_view *v, int view, int ax, int ay, int aw, int ah)
{
	const RECT *s = &art_views[view];
	v->sx = s->left;
	v->sy = s->top;
	v->sw = s->right - s->left;
	v->sh = s->bottom - s->top;
	double kx = (double)aw / v->sw, ky = (double)ah / v->sh;
	v->k = kx < ky ? kx : ky;
	v->dw = (int)(v->sw * v->k);
	v->dh = (int)(v->sh * v->k);
	v->dx = ax + (aw - v->dw) / 2;
	v->dy = ay + (ah - v->dh) / 2;
}

static inline int art_x(const struct art_view *v, int x) { return v->dx + (int)((x - v->sx) * v->k); }
static inline int art_y(const struct art_view *v, int y) { return v->dy + (int)((y - v->sy) * v->k); }
static inline int art_len(const struct art_view *v, int n) { int l = (int)(n * v->k + 0.5); return l < 1 ? 1 : l; }

/* What the drawing needs to show; a snapshot taken under hori_lock. */
struct ctl_view {
	struct hori_instance s;
	struct hori_btn_map map[HB_COUNT];
	int connected;
};

static void ctl_snapshot(struct ctl_view *v)
{
	v->s = hori_inst;
	memcpy(v->map, hori_map, sizeof(v->map));
	v->connected = hori_connected;
}

static int ctl_have(const struct ctl_view *v, int b)
{
	const struct hori_btn_map *m = &v->map[b];
	if( !v->connected )
		return 0;
	if( m->bit >= 0 && hori_word_bit_have(&v->s, m->bit) )
		return 1;
	return m->chan >= 0 && v->s.have_ir;
}

static void art_geo_fill(HDC dc, const struct art_view *v, const struct art_geo *g, int shrink, COLORREF col)
{
	if( G_CIRCLE == g->kind )
		d_circle(dc, art_x(v, g->x), art_y(v, g->y), art_len(v, g->w - shrink), col, 1, 0, 0);
	else
		d_rrect(dc, art_x(v, g->x + shrink), art_y(v, g->y + shrink), art_len(v, g->w - 2 * shrink),
				art_len(v, g->h - 2 * shrink), art_len(v, 8), col, 1, 0, 0);
}

static void art_geo_highlight(HDC dc, const struct art_view *v, const struct art_geo *g)
{
	int gap = 4 + d_pulse();
	if( G_CIRCLE == g->kind )
		d_circle(dc, art_x(v, g->x), art_y(v, g->y), art_len(v, g->w) + gap, 0, 0, GUI_HL, 3);
	else
		d_rrect(dc, art_x(v, g->x) - gap, art_y(v, g->y) - gap, art_len(v, g->w) + 2 * gap,
				art_len(v, g->h) + 2 * gap, 12, 0, 0, GUI_HL, 3);
}

/* Draw (part of) the controller with what's pressed, positions and the wizard's highlight. */
static void art_draw(HDC dc, const struct art_view *v, const struct ctl_view *cv, int hl)
{
	const struct hori_instance *s = &cv->s;
	int have_ir = cv->connected && s->have_ir;
	int have_mode = cv->connected && s->have_vr01;

	d_rrect(dc, v->dx - 1, v->dy - 1, v->dw + 2, v->dh + 2, 0, GUI_WHITE, 1, GUI_BORDER, 1);
	if( art_dc ) {
		SetStretchBltMode(dc, HALFTONE);
		SetBrushOrgEx(dc, 0, 0, NULL);
		StretchBlt(dc, v->dx, v->dy, v->dw, v->dh, art_dc,
				v->sx * 3 / 4, v->sy * 3 / 4, v->sw * 3 / 4, v->sh * 3 / 4, SRCCOPY);
	} else
		gui_text(dc, v->dx + 10, v->dy + 10, GUI_BAD, "The controller drawing is missing from this build.");

	int saved = SaveDC(dc);
	IntersectClipRect(dc, v->dx, v->dy, v->dx + v->dw, v->dy + v->dh);

	/* the mode LEDs light up like the real ones */
	for( int m=0; m < 3; m++ ) {
		const RECT *r = &art_leds[m];
		COLORREF c = !have_mode ? GUI_LED_NA : (s->mode == m ? GUI_LED_ON : GUI_LED_OFF);
		d_rrect(dc, art_x(v, r->left), art_y(v, r->top), art_len(v, r->right - r->left),
				art_len(v, r->bottom - r->top), 0, c, 1, 0, 0);
	}

	/* buttons: green when pressed; pressure buttons fill up as they're pressed */
	for( int i=0; i < ART_GEO_N; i++ ) {
		const struct art_geo *g = &art_geo[i];
		if( g->id >= HB_COUNT )
			continue;
		int b = g->id, have = ctl_have(cv, b), on = have && s->btn[b];
		if( on )
			art_geo_fill(dc, v, g, G_CIRCLE == g->kind ? 2 : 2, GUI_LIT);
		if( have && cv->map[b].chan >= 0 && have_ir && G_CIRCLE == g->kind ) {
			int p = s->axis_raw[AX_A + cv->map[b].chan];
			int r = (g->w - 3) * p / 255;
			if( r > 1 )
				d_circle(dc, art_x(v, g->x), art_y(v, g->y), art_len(v, r), on ? GUI_PRESS2 : GUI_PRESS, 1, 0, 0);
		}
	}

	if( have_ir ) {
		/* hat position */
		d_circle(dc, art_x(v, 1050 + (s->axis_raw[AX_HATX] - 128) * 22 / 128),
				art_y(v, 178 + (s->axis_raw[AX_HATY] - 128) * 22 / 128), art_len(v, 9), GUI_CENTER, 1, 0, 0);
		/* rudder and throttle positions */
		d_rrect(dc, art_x(v, 492 + s->axis_raw[AX_RUD] * 150 / 255), art_y(v, 160), art_len(v, 9), art_len(v, 38),
				art_len(v, 4), GUI_CENTER, 1, 0, 0);
		d_rrect(dc, art_x(v, 488 + s->axis_raw[AX_THR] * 245 / 255), art_y(v, 262), art_len(v, 10), art_len(v, 64),
				art_len(v, 4), GUI_CENTER, 1, 0, 0);
	}

	/* the stick's own position (not part of the drawing) */
	int bx = art_x(v, 1300), by = art_y(v, 700), bs = art_len(v, 150);
	d_rrect(dc, bx, by, bs, bs, art_len(v, 16), GUI_WHITE, 1, GUI_LINE, 2);
	d_line(dc, bx + bs / 2, by + 3, bx + bs / 2, by + bs - 3, GUI_OFF, 1);
	d_line(dc, bx + 3, by + bs / 2, bx + bs - 3, by + bs / 2, GUI_OFF, 1);
	if( have_ir )
		d_circle(dc, art_x(v, 1375 + (s->axis_raw[AX_X] - 128) * 64 / 128),
				art_y(v, 775 + (s->axis_raw[AX_Y] - 128) * 64 / 128), art_len(v, 10), GUI_CENTER, 1, 0, 0);
	SelectObject(dc, gui_small);
	gui_text(dc, bx, by + bs + 3, GUI_TEXT, "STICK X/Y");
	SelectObject(dc, gui_font);

	/* what the wizard is asking for */
	if( hl >= 0 )
		for( int i=0; i < ART_GEO_N; i++ )
			if( art_geo[i].id == hl )
				art_geo_highlight(dc, v, &art_geo[i]);

	RestoreDC(dc, saved);
}

/* ---- logging ---- */

static int log_level = 0;               /* 0 = default, 1 = debug */
static int log_raw_lines = 200;
static int log_new_each_run = 0;
static char log_dir[MAX_PATH];
static char log_path[MAX_PATH];
static SYSTEMTIME log_start;
static DWORD log_t0;

#define LOG_MSG_MAX 2000
static char log_msgs[LOG_MSG_MAX][460];
static int log_msg_count = 0;

#define RAW_MIN 10
#define RAW_MAX 5000
struct raw_row {
	DWORD t;
	uint8_t ir[8], v0[2], v1[2], v3;
	int8_t mode;
	uint8_t btn[HB_COUNT];
	int16_t wiz_at;     /* wizard step (or 100 + prompt during the button step), -1 if not running */
};
static struct raw_row raw_ring[RAW_MAX];
static int raw_count = 0;
static struct raw_row raw_prev;
static int raw_wiz_first = -1, raw_wiz_end = -1;    /* rows of the last wizard run */

/* Wizard results for the log: a summary (always) and the measurements (debug level). */
static struct gbuf wiz_summary, wiz_detail;

static void gui_log_hook(const char *line)
{
	DWORD t = GetTickCount() - log_t0;
	snprintf(log_msgs[log_msg_count++ % LOG_MSG_MAX], sizeof(log_msgs[0]), "[%8.2f] %s", t / 1000.0, line);
}

static void gui_settings_load(void)
{
	char path[MAX_PATH];
	hori_ini_path(path, sizeof(path));
	log_level = GetPrivateProfileIntA("Logging", "Level", 0, path) ? 1 : 0;
	log_raw_lines = GetPrivateProfileIntA("Logging", "RawLines", 200, path);
	if( log_raw_lines < RAW_MIN ) log_raw_lines = RAW_MIN;
	if( log_raw_lines > RAW_MAX ) log_raw_lines = RAW_MAX;
	log_new_each_run = GetPrivateProfileIntA("Logging", "NewFileEachRun", 0, path) ? 1 : 0;
}

static void gui_settings_save(void)
{
	char path[MAX_PATH], buf[16];
	hori_ini_path(path, sizeof(path));
	WritePrivateProfileStringA("Logging", "Level", log_level ? "1" : "0", path);
	snprintf(buf, sizeof(buf), "%d", log_raw_lines);
	WritePrivateProfileStringA("Logging", "RawLines", buf, path);
	WritePrivateProfileStringA("Logging", "NewFileEachRun", log_new_each_run ? "1" : "0", path);
}

static void gui_log_update_path(void)
{
	// "logs" next to the exe when portable, else %LOCALAPPDATA%\Flightstick2\logs
	char base[MAX_PATH];
	hori_data_dir(base, sizeof(base), "LOCALAPPDATA");
	snprintf(log_dir, sizeof(log_dir), "%s\\logs", base);
	CreateDirectoryA(log_dir, NULL);

	if( log_new_each_run )
		snprintf(log_path, sizeof(log_path), "%s\\hori-test-%04d%02d%02d-%02d%02d%02d.log", log_dir,
				log_start.wYear, log_start.wMonth, log_start.wDay,
				log_start.wHour, log_start.wMinute, log_start.wSecond);
	else
		snprintf(log_path, sizeof(log_path), "%s\\hori-test.log", log_dir);
}

static void gui_pressed_list(const uint8_t *btn, char *out, size_t size)
{
	out[0] = 0;
	for( int i=0; i < HB_COUNT; i++ )
		if( btn[i] )
			list_add(out, size, "%s", hori_btn_name[i]);
	if( !out[0] )
		snprintf(out, size, "-");
}

/* "signal 14", "pressure 2", "signal 13 + pressure 2" or "nothing" */
static void map_text(const struct hori_btn_map *m, char *out, size_t size)
{
	if( m->bit >= 0 && m->chan >= 0 )
		snprintf(out, size, "signal %d + pressure %d", m->bit, m->chan + 1);
	else if( m->bit >= 0 )
		snprintf(out, size, "signal %d", m->bit);
	else if( m->chan >= 0 )
		snprintf(out, size, "pressure %d", m->chan + 1);
	else
		snprintf(out, size, "nothing");
}


static void gui_log_cal(FILE *f)
{
	fprintf(f, "  %-20s %7s %7s %10s %10s %6s\n", "Axis", "Active", "Centre", "Range", "Dead zone", "Edge");
	for( int ax=0; ax < AX_COUNT; ax++ ) {
		const struct hori_cal *c = &hori_cal[ax];
		if( !c->enabled ) {
			fprintf(f, "  %-20s %7s\n", hori_axis_label(ax), "no");
			continue;
		}
		char centre[16];
		if( c->center >= 0 )
			snprintf(centre, sizeof(centre), "%d", c->center);
		else
			snprintf(centre, sizeof(centre), "-");
		fprintf(f, "  %-20s %7s %7s %6d-%-3d %10d %6d\n", hori_axis_label(ax), "yes", centre,
				c->min, c->max, c->deadzone, c->edge);
	}
	fprintf(f, "  Press threshold for pressure buttons: %d\n", opt_threshold);
}

static void raw_wiz_text(int wiz_at, char *out, size_t size);

/* The last log_raw_lines rows in [from, to), oldest first. Called with hori_lock held. */
static void gui_log_raw_table(FILE *f, const char *title, int from, int to)
{
	int start = to - log_raw_lines;
	if( start < from ) start = from;
	if( start < raw_count - RAW_MAX ) start = raw_count - RAW_MAX;

	fprintf(f, "\n-- %s: %d rows --\n", title, to - start);
	fprintf(f, "%9s | %4s %4s %4s %4s %4s %4s %4s %4s | %-14s | %-4s | %-20s | %s\n",
			"time(s)", "StkX", "StkY", "Rud", "Thr", "HatX", "HatY", "P1", "P2", "buttons raw", "mode", "wizard", "pressed");
	for( int i = start; i < to; i++ ) {
		const struct raw_row *r = &raw_ring[i % RAW_MAX];
		char mode[8], wz[48], pressed[400];
		if( r->mode >= 0 )
			snprintf(mode, sizeof(mode), "M%d", r->mode + 1);
		else
			snprintf(mode, sizeof(mode), "-");
		raw_wiz_text(r->wiz_at, wz, sizeof(wz));
		gui_pressed_list(r->btn, pressed, sizeof(pressed));
		fprintf(f, "%9.3f | %4d %4d %4d %4d %4d %4d %4d %4d | %02x %02x %02x %02x %02x | %-4s | %-20s | %s\n",
				r->t / 1000.0, r->ir[0], r->ir[1], r->ir[2], r->ir[3], r->ir[4], r->ir[5], r->ir[6], r->ir[7],
				r->v0[0], r->v0[1], r->v1[0], r->v1[1], r->v3, mode, wz, pressed);
	}
}

/* Write the whole log file (it's rewritten each time, so it always holds the full run). */
static int gui_log_write(void)
{
	FILE *f = fopen(log_path, "w");
	if( !f ) {
		hori_log("Could not write the log file %s\n", log_path);
		return -1;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);

	EnterCriticalSection(&hori_lock);

	fprintf(f, "Flightstick 2 test log\n");
	fprintf(f, "Run started: %04d-%02d-%02d %02d:%02d:%02d    Written: %04d-%02d-%02d %02d:%02d:%02d\n",
			log_start.wYear, log_start.wMonth, log_start.wDay, log_start.wHour, log_start.wMinute, log_start.wSecond,
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	fprintf(f, "Log level: %s", log_level ? "Debug" : "Default");
	if( log_level )
		fprintf(f, "    Raw lines kept: %d", log_raw_lines);
	fprintf(f, "\n\n");

	fprintf(f, "USB: %s\n", hori_connected ? "stick connected" : "stick not connected");
	if( hori_connected )
		fprintf(f, "Button requests: %s; request 0x03 %s\n", hori_vr_variant_name(hori_inst.vr_variant),
				hori_inst.have_vr03 ? "answers" : "doesn't answer");
	if( vj_ok )
		fprintf(f, "vJoy: device %u, %d buttons, %d POV hats, %ld updates sent, %ld failed\n",
				opt_vjoy_id, vj_buttons, vj_cont_povs + vj_disc_povs, vj_sent, vj_failed);
	else
		fprintf(f, "vJoy: not in use. %s\n", vj_error);
	fprintf(f, "\nDead zones in use:\n");
	gui_log_cal(f);

	fprintf(f, "\nButton signals in use (signal = bit in the button data, pressure = analog channel):\n");
	for( int i=0; i < HB_COUNT; i++ ) {
		char m[48];
		map_text(&hori_map[i], m, sizeof(m));
		fprintf(f, "  %-16s %s%s\n", hori_btn_name[i], m,
				memcmp(&hori_map[i], &hori_map_default[i], sizeof(hori_map[i])) ? "   (changed from default)" : "");
	}

	fprintf(f, "\n== Messages ==\n");
	int first = log_msg_count > LOG_MSG_MAX ? log_msg_count - LOG_MSG_MAX : 0;
	for( int i=first; i < log_msg_count; i++ )
		fprintf(f, "%s\n", log_msgs[i % LOG_MSG_MAX]);

	fprintf(f, "\n== Setup wizard ==\n");
	if( wiz_summary.len )
		fputs(wiz_summary.p, f);
	else
		fprintf(f, "(not run in this session)\n");
	if( log_level && wiz_detail.len ) {
		fprintf(f, "\n== Setup wizard measurements ==\n");
		fputs(wiz_detail.p, f);
	}

	if( log_level ) {
		fprintf(f, "\n== Raw sensor data ==\n");
		fprintf(f, "Axis columns are the raw bytes from the stick (P1/P2 = pressure channels: idle high, drop when pressed).\n");
		fprintf(f, "Buttons raw: the two button requests (2 bytes each) and request 0x03.\n");
		fprintf(f, "A row is logged when an axis moves by 3 or more, or a button, the mode, request 0x03 or the wizard step changes.\n");

		// The wizard run's rows get their own table, so idling afterwards doesn't push them out.
		if( raw_wiz_first >= 0 ) {
			int end = raw_wiz_end >= 0 ? raw_wiz_end : raw_count;
			if( raw_count - raw_wiz_first > RAW_MAX )
				fprintf(f, "\n-- During the last setup wizard run: no longer in memory --\n");
			else
				gui_log_raw_table(f, "During the last setup wizard run", raw_wiz_first, end);
		}
		gui_log_raw_table(f, "Most recent", 0, raw_count);
	}

	LeaveCriticalSection(&hori_lock);
	fclose(f);
	return 0;
}

/* ---- setup wizard ---- */

enum {
	WS_INTRO, WS_REST, WS_STICK, WS_STICK_RET, WS_HAT, WS_RUDDER, WS_THROTTLE, WS_A, WS_B, WS_BUTTONS, WS_RESULTS,
	WS_COUNT
};

enum { WK_ALL, WK_ANALOG, WK_BUTTONS };

static const struct { const char *title; const char *text; } wiz_steps[WS_COUNT] = {
	{ "Setup wizard",
	  "This checks the controls on your stick and sets it up to match. Pick what to test:\n\n"
	  "  Test everything: the analog controls, then every button (about 3 minutes).\n"
	  "  Analog only: the stick, hat, rudder, throttle and pressure buttons, and their dead zones.\n"
	  "  Buttons only: asks for each button in turn and checks which signal it really sends.\n\n"
	  "Each step finishes by itself once it has seen what it needs. If a control doesn't work, skip it." },
	{ "Hands off",
	  "Let go of the stick, hat and rudder, and don't touch anything (leave the throttle where it is).\n\n"
	  "This measures how much each sensor jitters at rest and where its centre is." },
	{ "Stick: full range",
	  "Move the stick slowly all the way to every edge and corner. Circling the full range a couple of times works well." },
	{ "Stick: centring",
	  "Push the stick all the way in any direction, then let go so it springs back by itself.\n\n"
	  "Do this 5 times, in different directions. Wait for it to stop before the next one." },
	{ "Hat",
	  "First move the hat all the way in every direction.\n\n"
	  "Then flick it to the side and let go so it springs back by itself, 3 times." },
	{ "Rudder control",
	  "First move the rudder control all the way left and right.\n\n"
	  "Then push it to one side and let go so it springs back to centre, 3 times." },
	{ "Throttle control",
	  "Slide the throttle from one end to the other 3 times (forward, back, forward)." },
	{ "Pressure 1", "" },     /* text built from the button that owns the channel */
	{ "Pressure 2", "" },
	{ "Buttons",
	  "Press and release each control the wizard asks for (it's highlighted on the drawing). "
	  "It records which signal each one really sends, so anything the original driver mixed up gets fixed." },
	{ "Results",
	  "Here's what the wizard found. Apply && save uses it now and saves it to hori.ini, so hori.exe uses it too." },
};

/* Prompts, in the order the controls sit on the stick and throttle.
 * Values >= PROMPT_MODE are mode switch positions. (The two dials aren't
 * here: they're mechanical and send nothing over USB.) */
#define PROMPT_MODE 100
static const int wiz_prompts[] = {
	HB_TRIGGER, HB_FIRE_C, HB_SW1, HB_LAUNCH, HB_A, HB_B, HB_HAT,
	HB_DPAD1_TOP, HB_DPAD1_BOTTOM, HB_DPAD1_LEFT, HB_DPAD1_RIGHT,
	HB_D, HB_ST, HB_DPAD2_TOP, HB_DPAD2_BOTTOM, HB_DPAD2_LEFT, HB_DPAD2_RIGHT,
	HB_DPAD3_TOP, HB_DPAD3_LEFT, HB_DPAD3_RIGHT,
	PROMPT_MODE + 1, PROMPT_MODE + 2, PROMPT_MODE + 0,
};
#define NPROMPTS (int)(sizeof(wiz_prompts) / sizeof(wiz_prompts[0]))

enum { PR_PENDING, PR_OK, PR_SKIPPED };

#define WIZ_SETTLES 32

struct wiz_axis {
	int rest_min, rest_max, rest_n;
	long rest_sum;
	int sw_min, sw_max;
	int settle[WIZ_SETTLES], nsettle;
	int press_max;
};

static struct {
	int active;
	int kind;
	int seq[WS_COUNT], nseq, pos;
	int step;
	DWORD step_start;
	int done;
	DWORD done_time;
	int ran[WS_COUNT];
	int skipped[WS_COUNT];
	int finished[WS_COUNT];
	DWORD took[WS_COUNT];

	struct wiz_axis ax[AX_COUNT];
	int press_floor[2];     /* lowest pressure seen on each channel: the resting level */

	/* hands off */
	int ref[AX_COUNT];
	DWORD rest_from;
	int rest_restarts;

	/* spring-back detection */
	int away;
	int settle_pending;     /* came back; note where it settles once it stops */
	DWORD return_time;      /* when the last spring-back was counted */
	int range_done;         /* hat/rudder: full range seen, now counting spring-backs */
	DWORD stable_since;
	int last[AX_COUNT];
	int returns;

	/* throttle: a move is counted once it has travelled far enough from where it turned round */
	int thr_turn, thr_ext, thr_dir, thr_counted, thr_changes;

	/* pressure */
	int pressed, presses;

	/* button prompts */
	int prompt;
	int pr_state[NPROMPTS];
	int pr_bit[NPROMPTS], pr_chan[NPROMPTS], pr_pmax[NPROMPTS];
	uint64_t base_word;
	int phase;              /* 0 = waiting for a press, 1 = pressed, waiting for release */
	uint64_t det_bits;
	int first_bit, det_chan, det_pmax;
	DWORD flash_time;       /* the box stays green briefly before the next prompt */
	DWORD mode_since;
	char pr_msg[200];


	/* results */
	struct hori_cal rec[AX_COUNT];
	int rec_threshold;
	char why[AX_COUNT][200];
	struct hori_btn_map rec_map[HB_COUNT];
	char btn_found[1200];
	int applied;
} wiz;

static HWND wiz_hwnd;

#define WIZ_W 940
#define WIZ_H 680
#define WIZ_ART_X 440
#define WIZ_ART_W (WIZ_W - WIZ_ART_X - 20)
#define WIZ_ART_H 470

static int wiz_center(int ax)
{
	const struct wiz_axis *w = &wiz.ax[ax];
	if( !w->rest_n )
		return 128;
	return (int)((w->rest_sum + w->rest_n / 2) / w->rest_n);
}

static int wiz_covered(int ax, int need)
{
	int c = wiz_center(ax);
	return wiz.ax[ax].sw_min <= c - need && wiz.ax[ax].sw_max >= c + need;
}

/* Axes each analog step watches; returns how many. */
static int wiz_step_axes(int step, int axes[2])
{
	switch( step ) {
	case WS_STICK:
	case WS_STICK_RET:  axes[0] = AX_X; axes[1] = AX_Y; return 2;
	case WS_HAT:        axes[0] = AX_HATX; axes[1] = AX_HATY; return 2;
	case WS_RUDDER:     axes[0] = AX_RUD; return 1;
	case WS_THROTTLE:   axes[0] = AX_THR; return 1;
	case WS_A:          axes[0] = AX_A; return 1;
	case WS_B:          axes[0] = AX_B; return 1;
	}
	return 0;
}

/* Which step measures an axis's range. */
static int wiz_range_step(int ax)
{
	switch( ax ) {
	case AX_X: case AX_Y:       return WS_STICK;
	case AX_HATX: case AX_HATY: return WS_HAT;
	case AX_RUD:                return WS_RUDDER;
	case AX_THR:                return WS_THROTTLE;
	case AX_A:                  return WS_A;
	default:                    return WS_B;
	}
}

/* The button that owns a pressure channel, or -1. */
static int wiz_chan_owner(int chan)
{
	for( int i=0; i < HB_COUNT; i++ )
		if( hori_map[i].chan == chan )
			return i;
	return -1;
}

/* What the drawing should point at for the current step. */
static int wiz_highlight(void)
{
	switch( wiz.step ) {
	case WS_STICK: case WS_STICK_RET:   return CTL_STICK;
	case WS_HAT:                        return CTL_HAT;
	case WS_RUDDER:                     return CTL_RUDDER;
	case WS_THROTTLE:                   return CTL_THROTTLE;
	case WS_A:                          return wiz_chan_owner(0);
	case WS_B:                          return wiz_chan_owner(1);
	case WS_BUTTONS: {
		if( wiz.prompt >= NPROMPTS )
			return -1;
		int p = wiz_prompts[wiz.prompt];
		if( p >= PROMPT_MODE )
			return CTL_MODE;
		return p;
	}
	}
	return -1;
}

static void wiz_prompt_name(int j, char *out, size_t size)
{
	int p = wiz_prompts[j];
	if( p >= PROMPT_MODE )
		snprintf(out, size, "Mode switch to M%d", p - PROMPT_MODE + 1);
	else
		snprintf(out, size, "%s", hori_btn_name[p]);
}

/* The wizard column of the raw log: step title, or the prompt during the button step. */
static void raw_wiz_text(int wiz_at, char *out, size_t size)
{
	if( wiz_at < 0 )
		snprintf(out, size, "-");
	else if( wiz_at >= 100 && wiz_at - 100 < NPROMPTS )
		wiz_prompt_name(wiz_at - 100, out, size);
	else if( WS_A == wiz_at || WS_B == wiz_at )
		snprintf(out, size, "%s", hori_axis_label(WS_A == wiz_at ? AX_A : AX_B));
	else if( wiz_at < WS_COUNT )
		snprintf(out, size, "%s", wiz_steps[wiz_at].title);
	else
		snprintf(out, size, "?");
}

static void wiz_compute(void);
static void wiz_compute_buttons(void);
static void wiz_report(void);

/* Ready the current button prompt. Called with hori_lock held. */
static void wiz_prompt_begin(void)
{
	wiz.base_word = hori_button_word(&hori_inst);
	wiz.phase = 0;
	wiz.det_bits = 0;
	wiz.first_bit = -1;
	wiz.det_chan = 0;
	wiz.det_pmax = 0;
	wiz.flash_time = 0;
	wiz.mode_since = 0;
	wiz.pr_msg[0] = 0;
}

/* Start a step. Called with hori_lock held. */
static void wiz_begin_step(int step)
{
	int axes[2], n, i;
	const int *v = hori_inst.axis_raw;

	wiz.step = step;
	wiz.step_start = GetTickCount();
	wiz.done = 0;
	wiz.ran[step] = 1;

	switch( step ) {
	case WS_REST:
		for( i=0; i < AX_COUNT; i++ ) {
			wiz.ax[i].rest_min = 255;
			wiz.ax[i].rest_max = 0;
			wiz.ax[i].rest_sum = 0;
			wiz.ax[i].rest_n = 0;
		}
		wiz.rest_from = wiz.step_start + 1000;     // a moment to let go
		wiz.rest_restarts = 0;
		break;

	case WS_BUTTONS:
		for( i=0; i < NPROMPTS; i++ ) {
			wiz.pr_state[i] = PR_PENDING;
			wiz.pr_bit[i] = -1;
			wiz.pr_chan[i] = -1;
			wiz.pr_pmax[i] = 0;
		}
		wiz.prompt = 0;
		wiz_prompt_begin();
		break;

	case WS_RESULTS:
		raw_wiz_end = raw_count;
		if( wiz.ran[WS_REST] || wiz.ran[WS_STICK] )
			wiz_compute();
		if( wiz.ran[WS_BUTTONS] )
			wiz_compute_buttons();
		wiz_report();
		break;

	default:
		n = wiz_step_axes(step, axes);
		for( i=0; i < n; i++ ) {
			struct wiz_axis *w = &wiz.ax[axes[i]];
			if( WS_STICK_RET != step ) {
				w->sw_min = 255;
				w->sw_max = 0;
			}
			if( WS_STICK != step )
				w->nsettle = 0;
			w->press_max = 0;
			wiz.last[axes[i]] = v[axes[i]];
		}
		wiz.away = 0;
		wiz.settle_pending = 0;
		wiz.range_done = 0;
		wiz.returns = 0;
		wiz.stable_since = wiz.step_start;
		wiz.thr_turn = v[AX_THR];
		wiz.thr_ext = v[AX_THR];
		wiz.thr_dir = 0;
		wiz.thr_counted = 0;
		wiz.thr_changes = 0;
		wiz.pressed = 0;
		wiz.presses = 0;
		break;
	}
}

static void wiz_step_done(void)
{
	wiz.done = 1;
	wiz.done_time = GetTickCount();
	wiz.finished[wiz.step] = 1;
	wiz.took[wiz.step] = wiz.done_time - wiz.step_start;
}

/* Move to the next button prompt (or finish the step). Called with hori_lock held. */
static void wiz_next_prompt(void)
{
	wiz.prompt++;
	if( wiz.prompt >= NPROMPTS )
		wiz_step_done();
	else
		wiz_prompt_begin();
}

/* After a spring-back is counted, pushes are ignored for this long, and until it
 * has settled (up to WIZ_RETURN_MAX): a control that flops past the centre
 * would otherwise count again. */
#define WIZ_RETURN_GAP  700
#define WIZ_RETURN_MAX  1500

static int wiz_return_wait(DWORD now)
{
	DWORD since = now - wiz.return_time;
	return wiz.returns && (since < WIZ_RETURN_GAP || (wiz.settle_pending && since < WIZ_RETURN_MAX));
}

/* Count "pushed away, then came back near the centre" as soon as it's back, and
 * note where it settles once it stops moving. */
static void wiz_track_returns(const int *axes, int n, int away, const int *v, DWORD now)
{
	int pushed = 0, centred = 1, moving = 0;
	for( int i=0; i < n; i++ ) {
		int a = axes[i];
		int d = abs(v[a] - wiz_center(a));
		if( d > away ) pushed = 1;
		if( d > 40 ) centred = 0;
		if( abs(v[a] - wiz.last[a]) > 3 ) moving = 1;    // more than the at-rest jitter
		wiz.last[a] = v[a];
	}
	if( moving )
		wiz.stable_since = now;

	if( pushed && !wiz_return_wait(now) ) {
		wiz.away = 1;
		wiz.settle_pending = 0;
	} else if( wiz.away && centred ) {
		wiz.returns++;
		wiz.away = 0;
		wiz.settle_pending = 1;
		wiz.return_time = now;
	}
	if( wiz.settle_pending && centred && !moving && now - wiz.stable_since >= 300 ) {
		for( int i=0; i < n; i++ ) {
			struct wiz_axis *w = &wiz.ax[axes[i]];
			if( w->nsettle < WIZ_SETTLES )
				w->settle[w->nsettle++] = v[axes[i]];
		}
		wiz.settle_pending = 0;
	}
}

/* Watch for the prompted button: which bit changes and/or which pressure channel rises,
 * then wait for it to be released before accepting. Called with hori_lock held. */
static void wiz_sample_prompt(struct hori_instance *inst, DWORD now)
{
	int p = wiz_prompts[wiz.prompt];
	char name[64];

	if( wiz.flash_time ) {
		if( now - wiz.flash_time >= 500 )
			wiz_next_prompt();
		return;
	}

	if( p >= PROMPT_MODE ) {
		if( inst->mode == p - PROMPT_MODE ) {
			if( !wiz.mode_since )
				wiz.mode_since = now;
			else if( now - wiz.mode_since >= 300 ) {
				wiz.pr_state[wiz.prompt] = PR_OK;
				wiz.flash_time = now;
			}
		} else
			wiz.mode_since = 0;
		return;
	}

	if( !inst->have_vr00 || !inst->have_vr01 )
		return;

	uint64_t word = hori_button_word(inst);
	uint64_t changed = (word ^ wiz.base_word) & ~HORI_MODE_BITS;
	int chans = 0, chans_up = 0;
	for( int k=0; k < 2; k++ ) {
		int pv = inst->axis_raw[AX_A + k];
		if( inst->have_ir && pv > wiz.press_floor[k] + 60 )
			chans |= 1 << k;
		if( inst->have_ir && pv > wiz.press_floor[k] + 15 )
			chans_up |= 1 << k;
		if( (wiz.det_chan & (1 << k)) && pv > wiz.det_pmax )
			wiz.det_pmax = pv;
	}

	if( changed || chans ) {
		if( changed && wiz.first_bit < 0 )
			for( int b=0; b < HORI_WORD_BITS; b++ )
				if( changed & (1ull << b) ) {
					wiz.first_bit = b;
					break;
				}
		wiz.det_bits |= changed;
		wiz.det_chan |= chans;
		wiz.phase = 1;
		return;
	}

	if( 1 != wiz.phase || chans_up )
		return;

	/* released: work out what it was */
	int bit = wiz.first_bit;
	int chan = (wiz.det_chan & 1) ? 0 : ((wiz.det_chan & 2) ? 1 : -1);

	for( int j=0; j < NPROMPTS; j++ ) {
		if( j == wiz.prompt || PR_OK != wiz.pr_state[j] || wiz_prompts[j] >= PROMPT_MODE )
			continue;
		if( (bit >= 0 && wiz.pr_bit[j] == bit) || (chan >= 0 && wiz.pr_chan[j] == chan) ) {
			wiz_prompt_name(j, name, sizeof(name));
			snprintf(wiz.pr_msg, sizeof(wiz.pr_msg),
					"That's the same signal as %s, which you already pressed. Try %s again.",
					name, hori_btn_name[p]);
			// Start this prompt over, keeping the message up.
			wiz.phase = 0;
			wiz.det_bits = 0;
			wiz.first_bit = -1;
			wiz.det_chan = 0;
			wiz.det_pmax = 0;
			wiz.base_word = word;
			return;
		}
	}

	wiz.pr_bit[wiz.prompt] = bit;
	wiz.pr_chan[wiz.prompt] = chan;
	wiz.pr_pmax[wiz.prompt] = wiz.det_pmax;
	wiz.pr_state[wiz.prompt] = PR_OK;
	wiz.pr_msg[0] = 0;
	wiz.flash_time = now;
}

/* Feed one poll's worth of data to the wizard. Called with hori_lock held. */
static void wiz_sample(struct hori_instance *inst)
{
	int axes[2], n, i;
	const int *v = inst->axis_raw;
	DWORD now = GetTickCount();

	if( inst->have_ir )
		for( int k=0; k < 2; k++ )
			if( v[AX_A + k] < wiz.press_floor[k] )
				wiz.press_floor[k] = v[AX_A + k];

	if( wiz.done )
		return;

	if( WS_BUTTONS == wiz.step ) {
		if( wiz.prompt < NPROMPTS )
			wiz_sample_prompt(inst, now);
		return;
	}

	if( !inst->have_ir )
		return;

	n = wiz_step_axes(wiz.step, axes);
	for( i=0; i < n; i++ ) {
		struct wiz_axis *w = &wiz.ax[axes[i]];
		if( v[axes[i]] < w->sw_min ) w->sw_min = v[axes[i]];
		if( v[axes[i]] > w->sw_max ) w->sw_max = v[axes[i]];
	}

	switch( wiz.step ) {
	case WS_REST:
		if( (LONG)(now - wiz.rest_from) < 0 ) {
			for( i=0; i < AX_COUNT; i++ )
				wiz.ref[i] = v[i];
			return;
		}
		for( i=0; i < AX_COUNT; i++ )
			if( abs(v[i] - wiz.ref[i]) > 12 )
				break;
		if( i < AX_COUNT ) {
			// Something moved: start the measurement over.
			for( i=0; i < AX_COUNT; i++ ) {
				wiz.ref[i] = v[i];
				wiz.ax[i].rest_min = 255;
				wiz.ax[i].rest_max = 0;
				wiz.ax[i].rest_sum = 0;
				wiz.ax[i].rest_n = 0;
			}
			wiz.rest_from = now + 500;
			wiz.rest_restarts++;
			return;
		}
		for( i=0; i < AX_COUNT; i++ ) {
			struct wiz_axis *w = &wiz.ax[i];
			if( v[i] < w->rest_min ) w->rest_min = v[i];
			if( v[i] > w->rest_max ) w->rest_max = v[i];
			w->rest_sum += v[i];
			w->rest_n++;
		}
		if( now - wiz.rest_from >= 3000 )
			wiz_step_done();
		break;

	case WS_STICK:
		if( wiz_covered(AX_X, 90) && wiz_covered(AX_Y, 90) )
			wiz_step_done();
		break;

	case WS_STICK_RET:
		wiz_track_returns(axes, n, 60, v, now);
		if( wiz.returns >= 5 && !wiz.settle_pending )
			wiz_step_done();
		break;

	case WS_HAT:
	case WS_RUDDER:
		// Range first; the moves for it don't count as spring-backs.
		if( !wiz.range_done ) {
			if( wiz_covered(axes[0], 70) && (n < 2 || wiz_covered(axes[1], 70)) ) {
				wiz.range_done = 1;
				wiz.away = 0;
				wiz.returns = 0;
				for( i=0; i < n; i++ )
					wiz.ax[axes[i]].nsettle = 0;
			}
			break;
		}
		wiz_track_returns(axes, n, 50, v, now);
		if( wiz.returns >= 3 && !wiz.settle_pending )
			wiz_step_done();
		break;

	case WS_THROTTLE: {
		int t = v[AX_THR];
		if( !wiz.thr_dir ) {
			if( abs(t - wiz.thr_turn) > 25 ) {
				wiz.thr_dir = t > wiz.thr_turn ? 1 : -1;
				wiz.thr_ext = t;
			}
		} else if( (t - wiz.thr_ext) * wiz.thr_dir > 0 )
			wiz.thr_ext = t;                // still going the same way
		else if( (wiz.thr_ext - t) * wiz.thr_dir > 25 ) {
			wiz.thr_turn = wiz.thr_ext;     // turned round
			wiz.thr_dir = -wiz.thr_dir;
			wiz.thr_ext = t;
			wiz.thr_counted = 0;
		}
		if( wiz.thr_dir && !wiz.thr_counted && abs(t - wiz.thr_turn) >= 140 ) {
			wiz.thr_counted = 1;
			wiz.thr_changes++;
		}
		if( wiz.thr_changes >= 3 )
			wiz_step_done();
		break;
	}

	case WS_A:
	case WS_B: {
		struct wiz_axis *w = &wiz.ax[axes[0]];
		int p = v[axes[0]];
		int idle = w->rest_n ? w->rest_max : wiz.press_floor[axes[0] - AX_A];
		if( p > w->press_max )
			w->press_max = p;
		if( !wiz.pressed && p > idle + 60 )
			wiz.pressed = 1;
		else if( wiz.pressed && p <= idle + 25 ) {
			wiz.pressed = 0;
			wiz.presses++;
		}
		if( wiz.presses >= 3 )
			wiz_step_done();
		break;
	}
	}
}

/* Work out recommended dead zones from what the wizard measured. Called with hori_lock held. */
static void wiz_compute(void)
{
	int thr = -1;

	for( int ax=0; ax < AX_COUNT; ax++ ) {
		struct wiz_axis *w = &wiz.ax[ax];
		struct hori_cal *r = &wiz.rec[ax];
		char *why = wiz.why[ax];
		size_t whysz = sizeof(wiz.why[ax]);
		int rstep = wiz_range_step(ax);
		int swept = wiz.finished[rstep] || (!wiz.skipped[rstep] && w->sw_max > w->sw_min);

		memset(r, 0, sizeof(*r));
		if( !w->rest_n ) {
			snprintf(why, whysz, "Not measured (hands-off step skipped), left as it is.");
			continue;
		}

		int noise = w->rest_max - w->rest_min;
		int c = wiz_center(ax);

		if( hori_axis_centered[ax] ) {
			int sdev = 0;
			for( int i=0; i < w->nsettle; i++ )
				if( abs(w->settle[i] - c) > sdev )
					sdev = abs(w->settle[i] - c);
			int dz = noise / 2 + 2;
			if( sdev + 2 > dz )
				dz = sdev + 2;
			if( dz > 40 )
				dz = 40;

			r->enabled = 1;
			r->center = c;
			r->deadzone = dz;
			if( swept && w->sw_max - w->sw_min >= 120 && w->sw_min < c && w->sw_max > c ) {
				r->min = w->sw_min;
				r->max = w->sw_max;
				r->edge = 2;
			} else {
				r->min = 0;
				r->max = 255;
			}

			int n = snprintf(why, whysz, "Centre %d, jitter %d", c, noise);
			if( w->nsettle )
				n += snprintf(why + n, whysz - n, ", springs back to within %d", sdev);
			else
				n += snprintf(why + n, whysz - n, ", spring-back not tested");
			if( !r->edge )
				n += snprintf(why + n, whysz - n, ", full range not seen");
			if( dz >= 20 )
				snprintf(why + n, whysz - n, ". Large: worn spring or sensor?");
		} else if( AX_THR == ax ) {
			int end = noise / 2 + 2;
			r->enabled = 1;
			r->center = -1;
			r->deadzone = end;
			r->edge = end;
			if( swept && w->sw_max - w->sw_min >= 100 ) {
				r->min = w->sw_min;
				r->max = w->sw_max;
				snprintf(why, whysz, "Moves %d-%d, jitter %d, so %d at each end", w->sw_min, w->sw_max, noise, end);
			} else {
				r->min = 0;
				r->max = 255;
				snprintf(why, whysz, "Jitter %d, so %d at each end (full travel not seen)", noise, end);
			}
		} else {
			int idle = w->rest_max;
			int tested = w->press_max > idle + 40;
			int pmax = tested ? w->press_max : 255;
			r->enabled = 1;
			r->center = -1;
			r->min = idle;
			r->deadzone = 2;
			r->max = pmax;
			r->edge = 3;
			int t = idle + ((pmax - idle) / 6 > 10 ? (pmax - idle) / 6 : 10);
			if( tested && t > thr )
				thr = t;
			if( tested )
				snprintf(why, whysz, "Rests at %d, full press %d, so a press counts above %d", idle, pmax, t);
			else
				snprintf(why, whysz, "Rests at %d, full press not tested (assuming %d)", idle, pmax);
		}
	}

	if( thr < 0 )
		thr = opt_threshold;
	if( thr < 5 ) thr = 5;
	if( thr > 200 ) thr = 200;
	wiz.rec_threshold = thr;
}

/* Work out the button map from the prompts. Called with hori_lock held. */
static void wiz_compute_buttons(void)
{
	struct hori_btn_map *m = wiz.rec_map;
	int tested[HB_COUNT] = { 0 };
	char changed[400] = "", missing[400] = "", pressure[200] = "";
	int ok = 0, total = 0, modes = 0;

	memcpy(m, hori_map, sizeof(wiz.rec_map));
	for( int j=0; j < NPROMPTS; j++ ) {
		int b = wiz_prompts[j];
		if( b >= PROMPT_MODE ) {
			modes += PR_OK == wiz.pr_state[j];
			if( PR_OK != wiz.pr_state[j] )
				list_add(missing, sizeof(missing), "M%d", b - PROMPT_MODE + 1);
			continue;
		}
		total++;
		if( PR_OK != wiz.pr_state[j] ) {
			list_add(missing, sizeof(missing), "%s", hori_btn_name[b]);
			continue;
		}
		ok++;
		m[b].bit = wiz.pr_bit[j];
		m[b].chan = wiz.pr_chan[j];
		tested[b] = 1;
	}

	// Untested buttons give up any signal a tested button turned out to use.
	for( int i=0; i < HB_COUNT; i++ ) {
		if( tested[i] )
			continue;
		for( int k=0; k < HB_COUNT; k++ ) {
			if( !tested[k] )
				continue;
			if( m[i].bit >= 0 && m[i].bit == m[k].bit )
				m[i].bit = -1;
			if( m[i].chan >= 0 && m[i].chan == m[k].chan )
				m[i].chan = -1;
		}
	}

	for( int i=0; i < HB_COUNT; i++ ) {
		if( m[i].chan >= 0 )
			list_add(pressure, sizeof(pressure), "%s", hori_btn_name[i]);
		if( memcmp(&m[i], &hori_map[i], sizeof(m[i])) ) {
			char a[48], b[48];
			map_text(&hori_map[i], a, sizeof(a));
			map_text(&m[i], b, sizeof(b));
			list_add(changed, sizeof(changed), "%s (%s -> %s)", hori_btn_name[i], a, b);
		}
	}

	const char *changes_intro = changed[0] ? "Changes: "
			: (ok ? "No changes needed: every button tested sends the signal expected." : "");
	snprintf(wiz.btn_found, sizeof(wiz.btn_found),
			"Buttons: %d of %d responded, mode switch %d of 3.\n"
			"Pressure-sensitive: %s.\n"
			"%s%s%s"
			"%s%s",
			ok, total, modes,
			pressure[0] ? pressure : "none",
			changes_intro, changed, changes_intro[0] ? "\n" : "",
			missing[0] ? "Not seen (skipped or not working): " : "", missing);
}

static void wiz_cal_text(const struct hori_cal *c, char *out, size_t size)
{
	if( !c->enabled )
		snprintf(out, size, "off");
	else if( c->center >= 0 )
		snprintf(out, size, "+-%d", c->deadzone);
	else
		snprintf(out, size, "%d / %d", c->deadzone, c->edge);
}

/* Add this run to the log text. Called with hori_lock held. */
static void wiz_report(void)
{
	static const char *const kinds[] = { "everything", "analog only", "buttons only" };
	SYSTEMTIME t;
	GetLocalTime(&t);

	gbuf_printf(&wiz_summary, "--- Run at %02d:%02d:%02d (%s) ---\n", t.wHour, t.wMinute, t.wSecond, kinds[wiz.kind]);
	for( int i=0; i < wiz.nseq; i++ ) {
		int s = wiz.seq[i];
		const char *title = (WS_A == s || WS_B == s) ? hori_axis_label(s == WS_A ? AX_A : AX_B) : wiz_steps[s].title;
		gbuf_printf(&wiz_summary, "  %-22s %s %.1f s\n", title,
				wiz.skipped[s] ? "skipped after" : "done in", wiz.took[s] / 1000.0);
	}

	if( wiz.ran[WS_REST] || wiz.ran[WS_STICK] ) {
		gbuf_printf(&wiz_summary, "Recommended dead zones (centred axes: +- around centre; others: low end / high end):\n");
		for( int ax=0; ax < AX_COUNT; ax++ ) {
			char now[32], rec[32];
			wiz_cal_text(&hori_cal[ax], now, sizeof(now));
			wiz_cal_text(&wiz.rec[ax], rec, sizeof(rec));
			gbuf_printf(&wiz_summary, "  %-20s %8s -> %-8s %s\n", hori_axis_label(ax), now, rec, wiz.why[ax]);
		}
		gbuf_printf(&wiz_summary, "  Press threshold: %d -> %d\n", opt_threshold, wiz.rec_threshold);
	}

	if( wiz.ran[WS_BUTTONS] ) {
		gbuf_printf(&wiz_summary, "%s\n", wiz.btn_found);
		gbuf_printf(&wiz_summary, "Each prompt:\n");
		for( int j=0; j < NPROMPTS; j++ ) {
			char name[64], m[300];
			int p = wiz_prompts[j];
			wiz_prompt_name(j, name, sizeof(name));
			if( PR_OK != wiz.pr_state[j] )
				snprintf(m, sizeof(m), "skipped");
			else if( p >= PROMPT_MODE )
				snprintf(m, sizeof(m), "OK");
			else {
				struct hori_btn_map bm = { wiz.pr_bit[j], wiz.pr_chan[j] };
				map_text(&bm, m, sizeof(m));
			}
			gbuf_printf(&wiz_summary, "  %-22s %s\n", name, m);
		}
	}

	gbuf_printf(&wiz_detail, "--- Run at %02d:%02d:%02d ---\n", t.wHour, t.wMinute, t.wSecond);
	gbuf_printf(&wiz_detail, "  %-20s %16s %12s %s\n", "Axis", "rest min/avg/max", "range seen", "spring-back rest points / full press");
	for( int ax=0; ax < AX_COUNT; ax++ ) {
		const struct wiz_axis *w = &wiz.ax[ax];
		char rest[32], range[32];
		if( w->rest_n )
			snprintf(rest, sizeof(rest), "%d/%d/%d", w->rest_min, wiz_center(ax), w->rest_max);
		else
			snprintf(rest, sizeof(rest), "-");
		if( w->sw_max >= w->sw_min && (w->sw_max || w->sw_min) )
			snprintf(range, sizeof(range), "%d-%d", w->sw_min, w->sw_max);
		else
			snprintf(range, sizeof(range), "-");
		gbuf_printf(&wiz_detail, "  %-20s %16s %12s ", hori_axis_label(ax), rest, range);
		if( AX_A == ax || AX_B == ax )
			gbuf_printf(&wiz_detail, "%d", w->press_max);
		else
			for( int i=0; i < w->nsettle; i++ )
				gbuf_printf(&wiz_detail, "%s%d", i ? " " : "", w->settle[i]);
		gbuf_printf(&wiz_detail, "\n");
	}
	gbuf_printf(&wiz_detail, "  Hands-off restarts (movement detected): %d\n", wiz.rest_restarts);
	gbuf_printf(&wiz_detail, "  Pressure resting levels: %d, %d\n", wiz.press_floor[0], wiz.press_floor[1]);
	if( wiz.ran[WS_BUTTONS] )
		for( int j=0; j < NPROMPTS; j++ )
			if( wiz.pr_chan[j] >= 0 ) {
				char name[64];
				wiz_prompt_name(j, name, sizeof(name));
				gbuf_printf(&wiz_detail, "  %s: pressure channel %d peaked at %d\n", name, wiz.pr_chan[j] + 1, wiz.pr_pmax[j]);
			}
}

static void wiz_place(int id, int x, int w, const char *text, int show)
{
	HWND c = GetDlgItem(wiz_hwnd, id);
	if( text )
		SetWindowTextA(c, text);
	SetWindowPos(c, NULL, x, WIZ_H - 50, w, 32, SWP_NOZORDER);
	ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

static void wiz_update_controls(void)
{
	int step = wiz.step;
	int intro = WS_INTRO == step, results = WS_RESULTS == step, buttons = WS_BUTTONS == step;
	int analog = !intro && !results && !buttons;

	wiz_place(IDC_WIZ_ALL, 20, 170, NULL, intro);
	wiz_place(IDC_WIZ_ANALOG, 200, 150, NULL, intro);
	wiz_place(IDC_WIZ_BUTTONS, 360, 150, NULL, intro);

	wiz_place(IDC_WIZ_BACK, 20, 80, NULL, buttons);
	EnableWindow(GetDlgItem(wiz_hwnd, IDC_WIZ_BACK), buttons && wiz.prompt > 0);
	wiz_place(IDC_WIZ_SKIPBTN, 110, 170, NULL, buttons);
	wiz_place(IDC_WIZ_SKIP, buttons ? 290 : 20, 150, buttons ? "Skip the rest" : "Skip this step", buttons || analog);

	wiz_place(IDC_WIZ_NEXT, WIZ_W - 190, 170, "Apply && save", results);
	wiz_place(IDC_WIZ_CANCEL, results ? WIZ_W - 400 : WIZ_W - 200, results ? 200 : 180,
			results ? "Close without applying" : "Cancel", 1);
}

/* Go to the next step in this run's sequence, or the results. */
static void wiz_advance(void)
{
	EnterCriticalSection(&hori_lock);
	wiz.pos++;
	int step = wiz.pos < wiz.nseq ? wiz.seq[wiz.pos] : WS_RESULTS;
	wiz_begin_step(step);
	LeaveCriticalSection(&hori_lock);
	wiz_update_controls();
	InvalidateRect(wiz_hwnd, NULL, FALSE);

	if( WS_RESULTS == step ) {
		hori_log("Setup wizard finished.\n");
		gui_log_write();
	}
}

static void wiz_start(int kind)
{
	static const int analog[] = { WS_REST, WS_STICK, WS_STICK_RET, WS_HAT, WS_RUDDER, WS_THROTTLE, WS_A, WS_B };
	static const char *const kinds[] = { "everything", "analog only", "buttons only" };

	EnterCriticalSection(&hori_lock);
	wiz.kind = kind;
	wiz.nseq = 0;
	if( WK_BUTTONS != kind )
		for( size_t i=0; i < sizeof(analog) / sizeof(analog[0]); i++ )
			wiz.seq[wiz.nseq++] = analog[i];
	if( WK_ANALOG != kind )
		wiz.seq[wiz.nseq++] = WS_BUTTONS;
	wiz.pos = -1;
	raw_wiz_first = raw_count;
	raw_wiz_end = -1;
	LeaveCriticalSection(&hori_lock);

	hori_log("Setup wizard started (%s).\n", kinds[kind]);
	wiz_advance();
}

static void wiz_skip(void)
{
	EnterCriticalSection(&hori_lock);
	int step = wiz.step;
	wiz.skipped[step] = 1;
	wiz.took[step] = GetTickCount() - wiz.step_start;
	const char *title = (WS_A == step || WS_B == step) ? hori_axis_label(step == WS_A ? AX_A : AX_B) : wiz_steps[step].title;
	LeaveCriticalSection(&hori_lock);
	hori_log("Setup wizard: skipped \"%s\".\n", title);
	wiz_advance();
}

static void wiz_skip_button(void)
{
	char name[64];
	EnterCriticalSection(&hori_lock);
	if( wiz.prompt < NPROMPTS ) {
		wiz_prompt_name(wiz.prompt, name, sizeof(name));
		wiz.pr_state[wiz.prompt] = PR_SKIPPED;
		wiz_next_prompt();
	} else
		name[0] = 0;
	LeaveCriticalSection(&hori_lock);
	if( name[0] )
		hori_log("Setup wizard: skipped %s.\n", name);
	wiz_update_controls();
}

static void wiz_back(void)
{
	EnterCriticalSection(&hori_lock);
	if( wiz.prompt > 0 ) {
		if( wiz.prompt < NPROMPTS )
			wiz.pr_state[wiz.prompt] = PR_PENDING;
		wiz.prompt--;
		wiz.pr_state[wiz.prompt] = PR_PENDING;
		wiz.pr_bit[wiz.prompt] = -1;
		wiz.pr_chan[wiz.prompt] = -1;
		wiz.done = 0;
		wiz_prompt_begin();
	}
	LeaveCriticalSection(&hori_lock);
	wiz_update_controls();
}

static void wiz_apply(void)
{
	EnterCriticalSection(&hori_lock);
	int cal = wiz.ran[WS_REST] || wiz.ran[WS_STICK], map = wiz.ran[WS_BUTTONS];
	if( cal ) {
		for( int ax=0; ax < AX_COUNT; ax++ )
			if( wiz.rec[ax].enabled )
				hori_cal[ax] = wiz.rec[ax];
		opt_threshold = wiz.rec_threshold;
	}
	if( map )
		memcpy(hori_map, wiz.rec_map, sizeof(hori_map));
	wiz.applied = 1;
	gbuf_printf(&wiz_summary, "Applied and saved to hori.ini.\n");
	LeaveCriticalSection(&hori_lock);

	if( cal )
		hori_cal_save();
	if( map )
		hori_map_save();
	hori_log("Setup wizard results applied and saved.\n");
	gui_log_write();
}

/* One axis bar for the wizard: swept range, rest centre and the current position. */
static void wiz_draw_axis(HDC dc, int x, int y, int ax, int value, int have)
{
	const struct wiz_axis *w = &wiz.ax[ax];
	const int bw = 300, bh = 22;
	int show_range = WS_STICK_RET != wiz.step && w->sw_max >= w->sw_min;

	gui_text(dc, x, y, have ? GUI_TEXT : GUI_DIM, "%s", hori_axis_label(ax));
	if( have )
		gui_text(dc, x + 180, y, GUI_TEXT, "now %d", value);
	if( show_range )
		gui_text(dc, x + 250, y, GUI_DIM, "seen %d-%d", w->sw_min, w->sw_max);
	y += 22;
	d_rrect(dc, x, y, bw, bh, 0, GUI_OFF, 1, GUI_BORDER, 1);
	if( show_range )
		d_rrect(dc, x + w->sw_min * bw / 255, y + 1, (w->sw_max - w->sw_min) * bw / 255 + 2, bh - 2, 0, GUI_SWEPT, 1, 0, 0);
	if( w->rest_n && hori_axis_centered[ax] )
		d_rrect(dc, x + wiz_center(ax) * bw / 255, y - 3, 2, bh + 6, 0, GUI_CENTER, 1, 0, 0);
	if( have )
		d_rrect(dc, x + value * bw / 255 - 2, y - 2, 5, bh + 4, 0, GUI_TEXT, 1, 0, 0);
}

static void wiz_paint(HDC dc)
{
	char buf[300];
	struct ctl_view v;

	EnterCriticalSection(&hori_lock);
	ctl_snapshot(&v);
	const struct hori_instance *s = &v.s;
	int step = wiz.step;
	DWORD now = GetTickCount();
	// The rest of the paint reads wiz directly; the worker only changes it under this lock.

	RECT all = { 0, 0, WIZ_W, WIZ_H };
	HBRUSH bg = CreateSolidBrush(GUI_BG);
	FillRect(dc, &all, bg);
	DeleteObject(bg);
	SetBkMode(dc, TRANSPARENT);

	int testing = step > WS_INTRO && step < WS_RESULTS;
	const char *title = wiz_steps[step].title;
	char title_buf[64];
	char text[700];
	if( WS_A == step || WS_B == step ) {
		int owner = wiz_chan_owner(WS_A == step ? 0 : 1);
		const char *bn = owner >= 0 ? hori_btn_name[owner] : "the pressure button";
		title = hori_axis_label(WS_A == step ? AX_A : AX_B);
		snprintf(text, sizeof(text), "Press %s all the way down and release it, 3 times.\n\n"
				"This measures its resting level and how far it goes when fully pressed.", bn);
	} else if( WS_HAT == step || WS_RUDDER == step ) {
		// Two parts, shown one at a time.
		int hat = WS_HAT == step;
		snprintf(title_buf, sizeof(title_buf), "%s: %s", hat ? "Hat" : "Rudder",
				wiz.range_done ? "spring-back" : "full range");
		title = title_buf;
		if( !wiz.range_done )
			snprintf(text, sizeof(text), "Part 1 of 2.\n\n%s", hat
					? "Move the hat slowly all the way in every direction. Circling it a couple of times works well."
					: "Move the rudder control all the way left and all the way right.");
		else
			snprintf(text, sizeof(text), "Part 2 of 2.\n\n%s\n\n"
					"Wait for it to stop before the next one; it counts once it's back in the middle.", hat
					? "Flick the hat to the side and let go so it springs back by itself. Do this 3 times."
					: "Push the rudder to one side and let go so it springs back by itself. Do this 3 times.");
	} else {
		// "&&" in the step text is for the button label; show a single "&".
		const char *src = wiz_steps[step].text;
		size_t o = 0;
		for( size_t i=0; src[i] && o + 1 < sizeof(text); i++ ) {
			if( '&' == src[i] && '&' == src[i + 1] )
				i++;
			text[o++] = src[i];
		}
		text[o] = 0;
	}

	if( testing )
		snprintf(buf, sizeof(buf), "Step %d of %d: %s", wiz.pos + 1, wiz.nseq, title);
	else
		snprintf(buf, sizeof(buf), "%s", title);
	gui_heading(dc, 20, 16, buf);

	int textw = testing ? WIZ_ART_X - 40 : WIZ_W - 40;
	gui_wrap(dc, 20, 50, textw, testing ? 120 : 200, GUI_TEXT, text);

	if( testing ) {
		int hl = wiz_highlight();
		struct art_view av;
		art_fit(&av, WS_REST == step ? V_ALL : ctl_art_view(hl), WIZ_ART_X, 50, WIZ_ART_W, WIZ_ART_H);
		art_draw(dc, &av, &v, hl);
	} else if( WS_INTRO == step ) {
		struct art_view av;
		art_fit(&av, V_ALL, 20, 260, WIZ_W - 40, WIZ_H - 330);
		art_draw(dc, &av, &v, -1);
	}

	/* progress line */
	int y = 176;
	COLORREF pc = GUI_TEXT;
	buf[0] = 0;
	if( testing && !v.connected ) {
		snprintf(buf, sizeof(buf), "The stick isn't connected.");
		pc = GUI_BAD;
	} else if( wiz.done ) {
		snprintf(buf, sizeof(buf), "Done!");
		pc = GUI_OK;
	} else switch( step ) {
	case WS_REST:
		if( (LONG)(now - wiz.rest_from) < 0 )
			snprintf(buf, sizeof(buf), "Get ready, hands off...");
		else
			snprintf(buf, sizeof(buf), "Holding still: %.1f of 3.0 s", (now - wiz.rest_from) / 1000.0);
		if( wiz.rest_restarts ) {
			size_t len = strlen(buf);
			snprintf(buf + len, sizeof(buf) - len, "  (restarted %d times)", wiz.rest_restarts);
			pc = GUI_WARN;
		}
		break;
	case WS_STICK:
		snprintf(buf, sizeof(buf), "Left/right: %s   Up/down: %s",
				wiz_covered(AX_X, 90) ? "done" : "keep going", wiz_covered(AX_Y, 90) ? "done" : "keep going");
		break;
	case WS_STICK_RET:
		snprintf(buf, sizeof(buf), "Spring-backs seen: %d of 5%s", wiz.returns,
				wiz.returns < 5 && wiz_return_wait(now) ? "   (wait for it to stop)" : "");
		break;
	case WS_HAT:
	case WS_RUDDER:
		if( wiz.range_done )
			snprintf(buf, sizeof(buf), "Spring-backs: %d of 3%s", wiz.returns,
					wiz.returns < 3 && wiz_return_wait(now) ? "   (wait for it to stop)" : "");
		else if( WS_HAT == step )
			snprintf(buf, sizeof(buf), "Left/right: %s   Up/down: %s",
					wiz_covered(AX_HATX, 70) ? "done" : "keep going", wiz_covered(AX_HATY, 70) ? "done" : "keep going");
		else
			snprintf(buf, sizeof(buf), "Left/right: %s", wiz_covered(AX_RUD, 70) ? "done" : "keep going");
		break;
	case WS_THROTTLE:
		snprintf(buf, sizeof(buf), "End-to-end moves: %d of 3", wiz.thr_changes);
		break;
	case WS_A:
	case WS_B:
		snprintf(buf, sizeof(buf), "Full presses: %d of 3", wiz.presses);
		break;
	case WS_BUTTONS: {
		int ok = 0;
		for( int j=0; j < NPROMPTS; j++ )
			ok += PR_OK == wiz.pr_state[j];
		snprintf(buf, sizeof(buf), "Control %d of %d   (%d working so far)",
				wiz.prompt < NPROMPTS ? wiz.prompt + 1 : NPROMPTS, NPROMPTS, ok);
		break;
	}
	}
	SelectObject(dc, gui_bold);
	gui_text(dc, 20, y, pc, "%s", buf);
	SelectObject(dc, gui_font);
	y += 40;

	/* live area (left column while testing) */
	int axes[2], n = wiz_step_axes(step, axes);
	if( WS_REST == step ) {
		for( int i=0; i < AX_COUNT; i++ ) {
			const struct wiz_axis *w = &wiz.ax[i];
			int yy = y + i * 26;
			gui_text(dc, 20, yy, GUI_TEXT, "%s", hori_axis_label(i));
			if( w->rest_n )
				gui_text(dc, 190, yy, GUI_TEXT, "%3d   jitter %d", s->axis_raw[i], w->rest_max - w->rest_min);
			else
				gui_text(dc, 190, yy, GUI_DIM, "%3d", s->axis_raw[i]);
		}
	} else if( n ) {
		for( int i=0; i < n; i++ )
			wiz_draw_axis(dc, 20, y + i * 70, axes[i], s->axis_raw[axes[i]], s->have_ir);
		gui_wrap(dc, 20, y + n * 70 + 6, WIZ_ART_X - 40, 60, GUI_DIM, "Green: range seen so far.  Blue: centre at rest.  Black: now.");
	} else if( WS_BUTTONS == step ) {
		char name[64];
		if( wiz.prompt < NPROMPTS ) {
			int p = wiz_prompts[wiz.prompt];
			wiz_prompt_name(wiz.prompt, name, sizeof(name));
			gui_text(dc, 20, y - 6, GUI_TEXT, p >= PROMPT_MODE ? "Move:" : "Press and release:");
			SelectObject(dc, gui_big);
			gui_text(dc, 20, y + 14, wiz.flash_time ? GUI_OK : GUI_HL, "%s", name);
			SelectObject(dc, gui_font);
			if( wiz.pr_msg[0] )
				gui_wrap(dc, 20, y + 52, WIZ_ART_X - 40, 44, GUI_BAD, wiz.pr_msg);
			else if( wiz.flash_time )
				gui_text(dc, 20, y + 52, GUI_OK, "Got it.");
			else if( 1 == wiz.phase )
				gui_text(dc, 20, y + 52, GUI_TEXT, "Seen, now let go.");
		}

		/* checklist */
		int ly = y + 128;
		int rows = (NPROMPTS + 1) / 2;
		SelectObject(dc, gui_small);
		for( int j=0; j < NPROMPTS; j++ ) {
			int col = j < rows ? 0 : 1, row = j < rows ? j : j - rows;
			int lx = 20 + col * 200, yy = ly + row * 17;
			COLORREF c = PR_OK == wiz.pr_state[j] ? GUI_OK : (PR_SKIPPED == wiz.pr_state[j] ? GUI_BAD : GUI_DIM);
			const char *mark = PR_OK == wiz.pr_state[j] ? "OK" : (PR_SKIPPED == wiz.pr_state[j] ? "--" : (j == wiz.prompt ? ">" : ""));
			wiz_prompt_name(j, name, sizeof(name));
			gui_text(dc, lx, yy, c, "%s", mark);
			gui_text(dc, lx + 22, yy, j == wiz.prompt ? GUI_TEXT : c, "%s", name);
		}
		SelectObject(dc, gui_font);
	} else if( WS_RESULTS == step ) {
		y = 90;
		if( wiz.ran[WS_REST] || wiz.ran[WS_STICK] ) {
			SelectObject(dc, gui_bold);
			gui_text(dc, 20, y, GUI_TEXT, "Axis");
			gui_text(dc, 190, y, GUI_TEXT, "Now");
			gui_text(dc, 250, y, GUI_TEXT, "New");
			gui_text(dc, 320, y, GUI_TEXT, "Why");
			SelectObject(dc, gui_font);
			y += 24;
			for( int ax=0; ax < AX_COUNT; ax++, y += 30 ) {
				char a[32], b[32];
				wiz_cal_text(&hori_cal[ax], a, sizeof(a));
				wiz_cal_text(&wiz.rec[ax], b, sizeof(b));
				gui_text(dc, 20, y, GUI_TEXT, "%s", hori_axis_label(ax));
				gui_text(dc, 190, y, GUI_DIM, "%s", a);
				gui_text(dc, 250, y, wiz.rec[ax].enabled ? GUI_OK : GUI_DIM, "%s", b);
				gui_wrap(dc, 320, y, WIZ_W - 336, 30, GUI_TEXT, wiz.why[ax]);
			}
			gui_text(dc, 20, y, GUI_TEXT, "Press threshold for pressure buttons: %d -> %d", opt_threshold, wiz.rec_threshold);
			y += 20;
			gui_text(dc, 20, y, GUI_DIM, "Centred axes: +- around the centre.  Throttle and pressure: low end / high end.");
			y += 30;
		}
		if( wiz.ran[WS_BUTTONS] ) {
			gui_heading(dc, 20, y, "Buttons");
			y += 26;
			gui_wrap(dc, 20, y, WIZ_W - 40, WIZ_H - 70 - y, GUI_TEXT, wiz.btn_found);
		}
		if( wiz.applied )
			gui_text(dc, 20, WIZ_H - 80, GUI_OK, "Applied and saved.");
	}

	LeaveCriticalSection(&hori_lock);
}

static LRESULT CALLBACK wiz_wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	switch( msg ) {
	case WM_CREATE:
		gui_control(w, 0, "BUTTON", "Test everything", BS_DEFPUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_ALL);
		gui_control(w, 0, "BUTTON", "Analog only", BS_PUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_ANALOG);
		gui_control(w, 0, "BUTTON", "Buttons only", BS_PUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_BUTTONS);
		gui_control(w, 0, "BUTTON", "Back", BS_PUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_BACK);
		gui_control(w, 0, "BUTTON", "Skip this button", BS_PUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_SKIPBTN);
		gui_control(w, 0, "BUTTON", "Skip this step", BS_PUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_SKIP);
		gui_control(w, 0, "BUTTON", "Cancel", BS_PUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_CANCEL);
		gui_control(w, 0, "BUTTON", "Apply && save", BS_DEFPUSHBUTTON, 0, 0, 10, 10, IDC_WIZ_NEXT);
		SetTimer(w, 1, 33, NULL);
		return 0;

	case WM_TIMER: {
		static int last_prompt = -1;
		EnterCriticalSection(&hori_lock);
		int advance = wiz.done && wiz.step > WS_INTRO && wiz.step < WS_RESULTS && GetTickCount() - wiz.done_time > 800;
		int prompt = wiz.prompt;
		LeaveCriticalSection(&hori_lock);
		if( advance )
			wiz_advance();
		else if( WS_BUTTONS == wiz.step && prompt != last_prompt )
			wiz_update_controls();     // Back enabled once past the first prompt
		last_prompt = WS_BUTTONS == wiz.step ? prompt : -1;
		InvalidateRect(w, NULL, FALSE);
		return 0;
	}

	case WM_COMMAND:
		switch( LOWORD(wp) ) {
		case IDC_WIZ_ALL:       wiz_start(WK_ALL); break;
		case IDC_WIZ_ANALOG:    wiz_start(WK_ANALOG); break;
		case IDC_WIZ_BUTTONS:   wiz_start(WK_BUTTONS); break;
		case IDC_WIZ_SKIP:      wiz_skip(); break;
		case IDC_WIZ_SKIPBTN:   wiz_skip_button(); break;
		case IDC_WIZ_BACK:      wiz_back(); break;
		case IDC_WIZ_NEXT:
			if( WS_RESULTS == wiz.step ) {
				wiz_apply();
				DestroyWindow(w);
			}
			break;
		case IDC_WIZ_CANCEL:
			SendMessageA(w, WM_CLOSE, 0, 0);
			break;
		}
		return 0;

	case WM_CLOSE:
		if( wiz.step < WS_RESULTS && wiz.step > WS_INTRO )
			hori_log("Setup wizard cancelled.\n");
		DestroyWindow(w);
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(w, &ps);
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP bmp = CreateCompatibleBitmap(dc, WIZ_W, WIZ_H);
		HGDIOBJ old = SelectObject(mem, bmp);
		SelectObject(mem, gui_font);
		wiz_paint(mem);
		BitBlt(dc, 0, 0, WIZ_W, WIZ_H, mem, 0, 0, SRCCOPY);
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(w, &ps);
		return 0;
	}

	case WM_DESTROY:
		EnterCriticalSection(&hori_lock);
		wiz.active = 0;
		if( raw_wiz_first >= 0 && raw_wiz_end < 0 )
			raw_wiz_end = raw_count;   // cancelled part-way
		LeaveCriticalSection(&hori_lock);
		wiz_hwnd = NULL;
		return 0;
	}
	return DefWindowProcA(w, msg, wp, lp);
}

static void wiz_open(void)
{
	if( wiz_hwnd ) {
		SetForegroundWindow(wiz_hwnd);
		return;
	}

	EnterCriticalSection(&hori_lock);
	memset(&wiz, 0, sizeof(wiz));
	wiz.active = 1;
	wiz.step = WS_INTRO;
	wiz.press_floor[0] = wiz.press_floor[1] = 255;
	LeaveCriticalSection(&hori_lock);

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
	RECT r = { 0, 0, WIZ_W, WIZ_H };
	AdjustWindowRect(&r, style, FALSE);
	wiz_hwnd = CreateWindowA("HoriWizard", "Setup wizard", style, CW_USEDEFAULT, CW_USEDEFAULT,
			r.right - r.left, r.bottom - r.top, gui_hwnd, NULL, GetModuleHandleA(NULL), NULL);
	wiz_update_controls();
	ShowWindow(wiz_hwnd, SW_SHOW);
}

/* ---- main test window ---- */

static void gui_sample_hook(struct hori_instance *inst)
{
	struct raw_row r;
	memcpy(r.ir, &inst->state_ir, 8);
	memcpy(r.v0, &inst->state_vr00, 2);
	memcpy(r.v1, &inst->state_vr01, 2);
	r.v3 = inst->state_vr03;
	r.mode = (int8_t)inst->mode;
	memcpy(r.btn, inst->btn, HB_COUNT);
	r.wiz_at = -1;
	if( wiz.active && wiz.step > WS_INTRO )
		r.wiz_at = (WS_BUTTONS == wiz.step && wiz.prompt < NPROMPTS) ? 100 + wiz.prompt : wiz.step;

	// A row when an axis moves by 3+ from the last logged row, or anything else changes;
	// sensors flicker by up to 2 at rest, which alone would fill the table in seconds.
	int moved = 0;
	for( int i=0; i < 8; i++ )
		if( abs((int)r.ir[i] - (int)raw_prev.ir[i]) >= 3 )
			moved = 1;
	if( moved || memcmp(r.v0, raw_prev.v0, 2) || memcmp(r.v1, raw_prev.v1, 2) || r.v3 != raw_prev.v3
			|| r.mode != raw_prev.mode || memcmp(r.btn, raw_prev.btn, HB_COUNT) || r.wiz_at != raw_prev.wiz_at ) {
		r.t = GetTickCount() - log_t0;
		raw_ring[raw_count++ % RAW_MAX] = r;
		raw_prev = r;
	}

	if( wiz.active )
		wiz_sample(inst);
}

/* Layout of the main window */
#define COL2_X      466                         /* the controller drawing */
#define TOP_Y       144
#define ART_DW      800
#define ART_DH      533
#define BELOW_Y     (TOP_Y + ART_DH + 14)       /* raw data line and below */
#define GUI_CTRL_Y  (BELOW_Y + 22 + 32 + 30)    /* first row of logging controls */

static void gui_paint(HDC dc)
{
	struct ctl_view v;
	int rate[4], logcount;
	char logs[HORI_LOG_LINES][160];
	struct hori_cal cal[AX_COUNT];
	char axname[AX_COUNT][48];

	EnterCriticalSection(&hori_lock);
	ctl_snapshot(&v);
	memcpy(rate, hori_ir_rate, sizeof(rate));
	memcpy(logs, hori_log_lines, sizeof(logs));
	memcpy(cal, hori_cal, sizeof(cal));
	logcount = hori_log_count;
	for( int i=0; i < AX_COUNT; i++ )
		snprintf(axname[i], sizeof(axname[i]), "%s", hori_axis_label(i));
	int hl = wiz_hwnd ? wiz_highlight() : -1;
	LeaveCriticalSection(&hori_lock);

	const struct hori_instance *s = &v.s;
	int conn = v.connected;

	RECT all = { 0, 0, GUI_W, GUI_H };
	HBRUSH bg = CreateSolidBrush(GUI_BG);
	FillRect(dc, &all, bg);
	DeleteObject(bg);
	SetBkMode(dc, TRANSPARENT);

	gui_heading(dc, 16, 12, "Flightstick 2 test");
	gui_text(dc, 200, 14, GUI_DIM, "Press each control. It should light up green on the drawing.");

	/* status */
	int y = 44;
	if( conn )
		gui_text(dc, 16, y, GUI_OK, "USB: stick connected");
	else
		gui_text(dc, 16, y, GUI_BAD, "USB: not connected, waiting for the stick (see messages)");
	y += 22;

	if( !conn )
		gui_text(dc, 16, y, GUI_DIM, "Buttons data: -");
	else if( s->vr_variant < 0 )
		gui_text(dc, 16, y, GUI_BAD, "Buttons data: the stick doesn't answer the button requests");
	else
		gui_text(dc, 16, y, (s->have_vr00 && s->have_vr01) ? GUI_OK : GUI_BAD,
				"Buttons data: %s, %s (request sent as %s)",
				s->have_vr00 ? "part 1 OK" : "part 1 NO DATA",
				s->have_vr01 ? "part 2 OK" : "part 2 NO DATA",
				hori_vr_variant_name(s->vr_variant));
	y += 22;

	if( !conn )
		gui_text(dc, 16, y, GUI_DIM, "Axes data: -");
	else
		gui_text(dc, 16, y, rate[0] > 0 ? GUI_OK : GUI_BAD,
				"Axes data, last second: %d good reads, %d short, %d timeouts, %d errors",
				rate[0], rate[1], rate[2], rate[3]);
	y += 22;

	if( vj_ok )
		gui_text(dc, 16, y, vj_failed ? GUI_BAD : GUI_OK,
				"vJoy: device %u, %d buttons, %d POV hats. Updates sent: %ld, failed: %ld",
				opt_vjoy_id, vj_buttons, vj_cont_povs + vj_disc_povs, vj_sent, vj_failed);
	else
		gui_text(dc, 16, y, GUI_BAD, "vJoy: not in use. %s", vj_error);

	/* axes (left column) */
	y = TOP_Y;
	gui_heading(dc, 16, y, "Axes");
	gui_text(dc, 70, y + 2, GUI_DIM, "(bar: what vJoy gets)");
	y += 26;
	for( int i=0; i < AX_COUNT; i++ ) {
		int ay = y + i * 28;
		gui_text(dc, 16, ay, s->have_ir ? GUI_TEXT : GUI_DIM, "%s", axname[i]);
		d_rrect(dc, 160, ay + 2, 200, 16, 0, GUI_OFF, 1, GUI_BORDER, 1);
		if( conn && s->have_ir ) {
			d_rrect(dc, 160, ay + 2, 1 + s->axis_out[i] * 200 / 255, 16, 0, GUI_LIT, 1, 0, 0);
			if( cal[i].enabled )
				gui_text(dc, 368, ay, GUI_TEXT, "%d -> %d", s->axis_raw[i], s->axis_out[i]);
			else
				gui_text(dc, 368, ay, GUI_TEXT, "%d", s->axis_raw[i]);
		} else
			gui_text(dc, 368, ay, GUI_DIM, "no data");
	}

	/* messages (left column, under the axes) */
	y = TOP_Y + 26 + AX_COUNT * 28 + 14;
	gui_heading(dc, 16, y, "Messages");
	y += 24;
	SelectObject(dc, gui_small);
	int shown = (BELOW_Y - 8 - y) / 17;
	int first = logcount > shown ? logcount - shown : 0;
	if( first < logcount - HORI_LOG_LINES )
		first = logcount - HORI_LOG_LINES;
	for( int i=first; i < logcount; i++, y += 17 ) {
		RECT mr = { 16, y, COL2_X - 12, y + 17 };
		SetTextColor(dc, GUI_TEXT);
		DrawTextA(dc, logs[i % HORI_LOG_LINES], -1, &mr, DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE);
	}
	SelectObject(dc, gui_font);

	/* the controller */
	struct art_view av;
	art_fit(&av, V_ALL, COL2_X, TOP_Y, ART_DW, ART_DH);
	art_draw(dc, &av, &v, hl);

	/* raw data */
	y = BELOW_Y;
	const uint8_t *ir = (const uint8_t *)&s->state_ir, *v0 = (const uint8_t *)&s->state_vr00, *v1 = (const uint8_t *)&s->state_vr01;
	char v3[8] = "-";
	if( s->have_vr03 )
		snprintf(v3, sizeof(v3), "%02x", s->state_vr03);
	gui_text(dc, 16, y, GUI_DIM, "Raw: axes %02x %02x %02x %02x %02x %02x %02x %02x   buttons %02x %02x  %02x %02x   req 0x03: %s",
			ir[0], ir[1], ir[2], ir[3], ir[4], ir[5], ir[6], ir[7], v0[0], v0[1], v1[0], v1[1], v3);
	gui_text(dc, 640, y, GUI_DIM, "Lights up here but not in joy.cpl: vJoy side.  Doesn't light up here: stick/USB side.");
	y += 22 + 10;

	/* settings & logging (the controls themselves are child windows) */
	gui_heading(dc, 16, y, "Logging and setup");
	y = GUI_CTRL_Y;
	gui_text(dc, 16, y + 4, GUI_TEXT, "Log level");
	gui_text(dc, 206, y + 4, log_level ? GUI_TEXT : GUI_DIM, "Raw lines kept");

	/* right of the wizard / reset buttons: log file and dead zones in use */
	y += 36;
	char logline[MAX_PATH + 32];
	snprintf(logline, sizeof(logline), "Log file: %s", log_path);
	RECT lr = { 500, y - 2, GUI_W - 16, y + 16 };
	SetTextColor(dc, GUI_DIM);
	DrawTextA(dc, logline, -1, &lr, DT_PATH_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE);

	char dz[400] = "";
	for( int i=0; i < AX_COUNT; i++ ) {
		if( !cal[i].enabled )
			continue;
		if( cal[i].center >= 0 )
			list_add(dz, sizeof(dz), "%s +-%d", hori_axis_name[i], cal[i].deadzone);
		else
			list_add(dz, sizeof(dz), "%s %d/%d", hori_axis_name[i], cal[i].deadzone, cal[i].edge);
	}
	RECT dr = { 500, y + 16, GUI_W - 16, y + 34 };
	if( dz[0] ) {
		char line[460];
		snprintf(line, sizeof(line), "Dead zones: %s", dz);
		SetTextColor(dc, GUI_TEXT);
		DrawTextA(dc, line, -1, &dr, DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE);
	} else
		gui_text(dc, 500, y + 16, GUI_DIM, "Dead zones: none yet. Run the setup wizard to set them.");
}

static void gui_update_rawlines_enabled(void)
{
	EnableWindow(gui_rawlines, log_level == 1);
	EnableWindow(gui_rawspin, log_level == 1);
}

static void gui_create_controls(HWND w)
{
	int y = GUI_CTRL_Y;

	HWND level = gui_control(w, 0, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 86, y, 104, 200, IDC_LEVEL);
	SendMessageA(level, CB_ADDSTRING, 0, (LPARAM)"Default");
	SendMessageA(level, CB_ADDSTRING, 0, (LPARAM)"Debug");
	SendMessageA(level, CB_SETCURSEL, log_level, 0);

	char num[16];
	snprintf(num, sizeof(num), "%d", log_raw_lines);
	gui_rawlines = gui_control(w, WS_EX_CLIENTEDGE, "EDIT", num, ES_NUMBER | WS_TABSTOP, 304, y, 74, 26, IDC_RAWLINES);
	gui_rawspin = CreateWindowExA(0, UPDOWN_CLASSA, NULL,
			WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
			0, 0, 0, 0, w, (HMENU)(INT_PTR)IDC_RAWSPIN, GetModuleHandleA(NULL), NULL);
	SendMessageA(gui_rawspin, UDM_SETBUDDY, (WPARAM)gui_rawlines, 0);
	SendMessageA(gui_rawspin, UDM_SETRANGE32, RAW_MIN, RAW_MAX);
	SendMessageA(gui_rawspin, UDM_SETPOS32, 0, log_raw_lines);
	UDACCEL accel[] = { { 0, 10 }, { 1, 50 }, { 2, 200 } };
	SendMessageA(gui_rawspin, UDM_SETACCEL, 3, (LPARAM)accel);

	HWND mode = gui_control(w, 0, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 392, y, 206, 200, IDC_LOGMODE);
	SendMessageA(mode, CB_ADDSTRING, 0, (LPARAM)"Overwrite the log each run");
	SendMessageA(mode, CB_ADDSTRING, 0, (LPARAM)"New log file each run");
	SendMessageA(mode, CB_SETCURSEL, log_new_each_run, 0);

	gui_control(w, 0, "BUTTON", "Save log now", BS_PUSHBUTTON | WS_TABSTOP, 608, y - 1, 110, 28, IDC_SAVELOG);
	gui_control(w, 0, "BUTTON", "Open log folder", BS_PUSHBUTTON | WS_TABSTOP, 724, y - 1, 124, 28, IDC_OPENLOGS);

	y += 36;
	gui_control(w, 0, "BUTTON", "Setup wizard...", BS_PUSHBUTTON | WS_TABSTOP, 16, y, 150, 30, IDC_WIZARD);
	gui_control(w, 0, "BUTTON", "Reset dead zones", BS_PUSHBUTTON | WS_TABSTOP, 176, y, 150, 30, IDC_RESETCAL);
	gui_control(w, 0, "BUTTON", "Setup check...", BS_PUSHBUTTON | WS_TABSTOP, 336, y, 150, 30, IDC_SETUP);

	gui_control(w, 0, "BUTTON", "About...", BS_PUSHBUTTON | WS_TABSTOP, GUI_W - 116, 10, 100, 28, IDC_ABOUT);

	gui_update_rawlines_enabled();
}

static void gui_command(HWND w, int id, int code)
{
	switch( id ) {
	case IDC_LEVEL:
		if( CBN_SELCHANGE == code ) {
			log_level = (int)SendMessageA(GetDlgItem(w, IDC_LEVEL), CB_GETCURSEL, 0, 0) ? 1 : 0;
			gui_update_rawlines_enabled();
			gui_settings_save();
		}
		break;

	case IDC_RAWLINES:
		if( EN_CHANGE == code ) {
			char buf[16];
			GetWindowTextA(gui_rawlines, buf, sizeof(buf));
			int v = atoi(buf);
			if( v >= RAW_MIN && v <= RAW_MAX && v != log_raw_lines ) {
				log_raw_lines = v;
				gui_settings_save();
			}
		} else if( EN_KILLFOCUS == code ) {
			// Put an out-of-range entry back to what's actually used.
			SendMessageA(gui_rawspin, UDM_SETPOS32, 0, log_raw_lines);
		}
		break;

	case IDC_LOGMODE:
		if( CBN_SELCHANGE == code ) {
			log_new_each_run = (int)SendMessageA(GetDlgItem(w, IDC_LOGMODE), CB_GETCURSEL, 0, 0) ? 1 : 0;
			gui_log_update_path();
			gui_settings_save();
		}
		break;

	case IDC_SAVELOG:
		if( 0 == gui_log_write() )
			hori_log("Log saved to %s\n", log_path);
		break;

	case IDC_OPENLOGS:
		ShellExecuteA(w, "open", log_dir, NULL, NULL, SW_SHOWNORMAL);
		break;

	case IDC_WIZARD:
		wiz_open();
		break;

	case IDC_SETUP:
		setup_open(w);
		break;

	case IDC_ABOUT:
		about_open(w);
		break;

	case IDC_RESETCAL:
		if( IDYES != MessageBoxA(w, "Remove all dead zones and go back to the raw stick values?\n\n"
				"(Button signals found by the wizard are kept.)",
				"Reset dead zones", MB_YESNO | MB_ICONQUESTION) )
			break;
		EnterCriticalSection(&hori_lock);
		memset(hori_cal, 0, sizeof(hori_cal));
		opt_threshold = 20;
		LeaveCriticalSection(&hori_lock);
		hori_cal_save();
		hori_log("Dead zones reset.\n");
		break;
	}
}

/* ---- tray ----
 * Closing the window hides it; the tray icon keeps the driver running.
 * Exit is on the tray icon's menu. */

#define WM_TRAY         (WM_APP + 20)
#define SHOW_MSG_NAME   "Flightstick2-Show"     /* a second launch asks the running one to show */
#define RUN_KEY         "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE       "Flightstick2"

enum { IDM_TRAY_OPEN = 601, IDM_TRAY_SETUP, IDM_TRAY_AUTOSTART, IDM_TRAY_EXIT };

static NOTIFYICONDATAW tray_nid;
static UINT msg_taskbar_created, msg_show;
static int tray_exiting;
static int tray_told;               /* the "still running" note has been shown */
static int tray_balloon_setup;      /* clicking the current notification opens the setup check */
static int tray_state = -1;
static HANDLE instance_mutex;

int hori_gui_single_instance(void)
{
	instance_mutex = CreateMutexA(NULL, TRUE, "Local\\Flightstick2-hori-test");
	if( instance_mutex && ERROR_ALREADY_EXISTS == GetLastError() ) {
		HWND other = FindWindowA("HoriTest", NULL);
		if( other ) {
			DWORD pid = 0;
			GetWindowThreadProcessId(other, &pid);
			AllowSetForegroundWindow(pid);
			PostMessageA(other, RegisterWindowMessageA(SHOW_MSG_NAME), 0, 0);
		}
		return 0;
	}
	return 1;
}

/* Start with Windows: a per-user Run entry that starts this exe in the tray. */
static int autostart_get(void)
{
	char buf[MAX_PATH + 16];
	DWORD len = sizeof(buf);
	return ERROR_SUCCESS == RegGetValueA(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, NULL, buf, &len);
}

static void autostart_set(int on)
{
	HKEY k;
	if( ERROR_SUCCESS != RegCreateKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) )
		return;
	if( on ) {
		char exe[MAX_PATH], cmd[MAX_PATH + 16];
		GetModuleFileNameA(NULL, exe, sizeof(exe));
		snprintf(cmd, sizeof(cmd), "\"%s\" -T", exe);
		RegSetValueExA(k, RUN_VALUE, 0, REG_SZ, (const BYTE *)cmd, (DWORD)strlen(cmd) + 1);
	} else
		RegDeleteValueA(k, RUN_VALUE);
	RegCloseKey(k);
}

static void tray_add(HWND w)
{
	memset(&tray_nid, 0, sizeof(tray_nid));
	tray_nid.cbSize = sizeof(tray_nid);
	tray_nid.hWnd = w;
	tray_nid.uID = 1;
	tray_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
	tray_nid.uCallbackMessage = WM_TRAY;
	tray_nid.hIcon = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1), IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	_snwprintf(tray_nid.szTip, sizeof(tray_nid.szTip) / sizeof(WCHAR), L"Flightstick 2");
	Shell_NotifyIconW(NIM_ADD, &tray_nid);
	tray_nid.uVersion = NOTIFYICON_VERSION_4;
	Shell_NotifyIconW(NIM_SETVERSION, &tray_nid);
	tray_state = -1;
}

static void tray_balloon(const char *title, const char *text, int opens_setup)
{
	tray_nid.uFlags = NIF_INFO;
	_snwprintf(tray_nid.szInfoTitle, sizeof(tray_nid.szInfoTitle) / sizeof(WCHAR), L"%hs", title);
	_snwprintf(tray_nid.szInfo, sizeof(tray_nid.szInfo) / sizeof(WCHAR), L"%hs", text);
	tray_nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
	tray_nid.hBalloonIcon = tray_nid.hIcon;
	Shell_NotifyIconW(NIM_MODIFY, &tray_nid);
	tray_balloon_setup = opens_setup;
}

/* Keep the tray tooltip saying whether the stick is working. */
static void tray_update_tip(void)
{
	static const char *const text[] = {
		"stick connected", "waiting for the stick", "vJoy isn't set up (open Setup check)",
	};
	EnterCriticalSection(&hori_lock);
	int state = !vj_ok ? 2 : (hori_connected ? 0 : 1);
	LeaveCriticalSection(&hori_lock);
	if( state == tray_state )
		return;
	tray_state = state;
	tray_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
	_snwprintf(tray_nid.szTip, sizeof(tray_nid.szTip) / sizeof(WCHAR), L"Flightstick 2: %hs", text[state]);
	Shell_NotifyIconW(NIM_MODIFY, &tray_nid);
}

static void gui_show(HWND w)
{
	ShowWindow(w, IsIconic(w) ? SW_RESTORE : SW_SHOW);
	SetForegroundWindow(w);
}

static void tray_menu(HWND w, int x, int y)
{
	HMENU m = CreatePopupMenu();
	AppendMenuA(m, MF_STRING, IDM_TRAY_OPEN, "Open Flightstick 2");
	AppendMenuA(m, MF_STRING, IDM_TRAY_SETUP, "Setup check...");
	AppendMenuA(m, MF_STRING | (autostart_get() ? MF_CHECKED : 0), IDM_TRAY_AUTOSTART, "Start with Windows");
	AppendMenuA(m, MF_SEPARATOR, 0, NULL);
	AppendMenuA(m, MF_STRING, IDM_TRAY_EXIT, "Exit");
	SetMenuDefaultItem(m, IDM_TRAY_OPEN, FALSE);
	SetForegroundWindow(w);     // so the menu closes when you click elsewhere
	TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, w, NULL);
	PostMessageA(w, WM_NULL, 0, 0);
	DestroyMenu(m);
}

static int tray_command(HWND w, int id)
{
	switch( id ) {
	case IDM_TRAY_OPEN:
		gui_show(w);
		return 1;
	case IDM_TRAY_SETUP:
		gui_show(w);
		setup_open(w);
		return 1;
	case IDM_TRAY_AUTOSTART: {
		int on = !autostart_get();
		autostart_set(on);
		hori_log("Start with Windows: %s.\n", on ? "on" : "off");
		return 1;
	}
	case IDM_TRAY_EXIT:
		tray_exiting = 1;
		DestroyWindow(w);
		return 1;
	}
	return 0;
}

static LRESULT CALLBACK gui_wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	if( msg && msg == msg_show ) {
		gui_show(w);
		return 0;
	}
	if( msg && msg == msg_taskbar_created ) {   // Explorer restarted: put the icon back
		tray_add(w);
		return 0;
	}

	switch( msg ) {
	case WM_CREATE:
		msg_show = RegisterWindowMessageA(SHOW_MSG_NAME);
		msg_taskbar_created = RegisterWindowMessageA("TaskbarCreated");
		gui_create_controls(w);
		tray_add(w);
		SetTimer(w, 1, 33, NULL);
		SetTimer(w, 2, 2500, NULL);    // setup check, once the stick has had time to connect
		SetTimer(w, 3, 1000, NULL);    // tray tooltip
		return 0;
	case WM_TIMER:
		if( 2 == wp ) {
			KillTimer(w, 2);
			if( !setup_all_ok() ) {
				if( IsWindowVisible(w) )
					setup_open(w);
				else
					tray_balloon("Flightstick 2 needs setting up",
							"Something the stick needs is missing. Click here to open the Setup check.", 1);
			}
			return 0;
		}
		if( 3 == wp ) {
			tray_update_tip();
			return 0;
		}
		if( IsWindowVisible(w) )
			InvalidateRect(w, NULL, FALSE);
		return 0;
	case WM_TRAY:
		switch( LOWORD(lp) ) {
		case NIN_SELECT:
		case NIN_KEYSELECT:
			gui_show(w);
			break;
		case WM_CONTEXTMENU:
			tray_menu(w, (short)LOWORD(wp), (short)HIWORD(wp));
			break;
		case NIN_BALLOONUSERCLICK:
			gui_show(w);
			if( tray_balloon_setup )
				setup_open(w);
			break;
		}
		return 0;
	case WM_CLOSE:
		if( tray_exiting )
			break;
		// Hide to the tray: games keep working.
		ShowWindow(w, SW_HIDE);
		if( !tray_told ) {
			tray_told = 1;
			tray_balloon("Flightstick 2 is still running",
					"It keeps the stick working in games. Right-click the tray icon to exit.", 0);
		}
		return 0;
	case WM_COMMAND:
		if( tray_command(w, LOWORD(wp)) )
			return 0;
		gui_command(w, LOWORD(wp), HIWORD(wp));
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(w, &ps);
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP bmp = CreateCompatibleBitmap(dc, GUI_W, GUI_H);
		HGDIOBJ old = SelectObject(mem, bmp);
		SelectObject(mem, gui_font);
		gui_paint(mem);
		BitBlt(dc, 0, 0, GUI_W, GUI_H, mem, 0, 0, SRCCOPY);
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(w, &ps);
		return 0;
	}
	case WM_DESTROY:
		Shell_NotifyIconW(NIM_DELETE, &tray_nid);
		if( wiz_hwnd )
			DestroyWindow(wiz_hwnd);
		gui_log_write();
		InterlockedExchange(&hori_quit, 1);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcA(w, msg, wp, lp);
}

/* Called before vJoy/USB start, so their messages make it into the log. */
void hori_gui_init(void)
{
	GetLocalTime(&log_start);
	log_t0 = GetTickCount();
	gui_settings_load();
	gui_log_update_path();

	EnterCriticalSection(&hori_lock);
	raw_prev.mode = -2;
	hori_log_hook = gui_log_hook;
	hori_sample_hook = gui_sample_hook;
	LeaveCriticalSection(&hori_lock);
}

void hori_gui(void)
{
	HINSTANCE hi = GetModuleHandleA(NULL);

	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
	InitCommonControlsEx(&icc);

	gui_font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
	gui_bold = CreateFontA(-17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
	gui_small = CreateFontA(-12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
	gui_big = CreateFontA(-26, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

	art_bmp = (HBITMAP)LoadImageA(hi, MAKEINTRESOURCEA(ART_RES_ID), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
	if( art_bmp ) {
		art_dc = CreateCompatibleDC(NULL);
		SelectObject(art_dc, art_bmp);
	}

	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.hInstance = hi;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIconA(hi, MAKEINTRESOURCEA(1));

	wc.lpfnWndProc = gui_wndproc;
	wc.lpszClassName = "HoriTest";
	RegisterClassA(&wc);

	wc.lpfnWndProc = wiz_wndproc;
	wc.lpszClassName = "HoriWizard";
	RegisterClassA(&wc);

	setup_register_class(hi);
	about_register_class(hi);

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	RECT r = { 0, 0, GUI_W, GUI_H };
	AdjustWindowRect(&r, style, FALSE);
	gui_hwnd = CreateWindowA("HoriTest", "Flightstick 2 test", style, CW_USEDEFAULT, CW_USEDEFAULT,
			r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
	ShowWindow(gui_hwnd, opt_tray ? SW_HIDE : SW_SHOW);

	MSG m;
	while( GetMessageA(&m, NULL, 0, 0) > 0 ) {
		if( IsDialogMessageA(GetActiveWindow(), &m) )
			continue;
		TranslateMessage(&m);
		DispatchMessageA(&m);
	}
}
