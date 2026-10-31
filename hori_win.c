/*
 * Windows port of the Mitsubishi HORI/Namco Flightstick 2 driver.
 *
 * Reads the stick through libusb (WinUSB driver, installed with Zadig) and
 * feeds a vJoy virtual joystick, which games see as a normal DirectInput device.
 *
 * The test window and setup wizard are in hori_gui.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>

#include "hori_win.h"

#define HORI_VENDOR_ID          0x06d3
#define HORI_PRODUCT_ID         0x0f10

#define HORI_POLL_VR0           0x00
#define HORI_POLL_VR1           0x01
#define HORI_POLL_VR3           0x03    /* 1 byte, meaning unknown (not used by hfsd) */

/* vJoy axis ids (HID usages) */
#define HID_USAGE_X     0x30
#define HID_USAGE_Y     0x31
#define HID_USAGE_Z     0x32
#define HID_USAGE_RX    0x33
#define HID_USAGE_RY    0x34
#define HID_USAGE_RZ    0x35
#define HID_USAGE_SL0   0x36
#define HID_USAGE_SL1   0x37
#define HID_USAGE_WHL   0x38

enum vjd_stat { VJD_STAT_OWN, VJD_STAT_FREE, VJD_STAT_BUSY, VJD_STAT_MISS, VJD_STAT_UNKN };

/* vJoy button numbers (1-based), same order as hfsd's defaults.
 * 10-12 are M1/M2/M3 with "-m buttons". D-PAD 1/2 use 16-23 only when the
 * vJoy device has fewer than 2 POV hats, D-PAD 3 uses 13-15 when it has
 * fewer than 3 (hfsd's left/middle/right). */
static const int hori_btn_num[HB_COUNT] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9,
	14, 15, 13,
	16, 17, 18, 19,
	20, 21, 22, 23,
};
#define HORI_MODE_BTN_BASE      10
#define HORI_LAYER_STRIDE       32      /* "-m shift": M2 adds 32, M3 adds 64 */

/* Names as printed on the controller. */
const char *const hori_btn_name[HB_COUNT] = {
	"Trigger", "Button C", "Launch", "Button A", "Button B", "Hat push", "SW-1", "Button D", "Button ST",
	"D-Pad 3 up", "D-Pad 3 right", "D-Pad 3 left",
	"D-Pad 1 up", "D-Pad 1 right", "D-Pad 1 down", "D-Pad 1 left",
	"D-Pad 2 up", "D-Pad 2 right", "D-Pad 2 down", "D-Pad 2 left",
};

const char *const hori_axis_name[AX_COUNT] = {
	"Stick X", "Stick Y", "Throttle", "Rudder", "Hat X", "Hat Y", "Pressure 1", "Pressure 2",
};

/* hori.ini keys for the button map */
static const char *const hori_btn_key[HB_COUNT] = {
	"Trigger", "ButtonC", "Launch", "ButtonA", "ButtonB", "HatPush", "SW1", "ButtonD", "ButtonST",
	"Pad3Up", "Pad3Right", "Pad3Left",
	"Pad1Up", "Pad1Right", "Pad1Down", "Pad1Left",
	"Pad2Up", "Pad2Right", "Pad2Down", "Pad2Left",
};

/* The bit positions of hfsd's vendor request layout (see hori_win.h's
 * structs: vr00 is bits 0-15, vr01 bits 16-31); A and B are the pressure channels.
 * D-PAD 3 has three positions; hfsd's "middle" is up. */
const struct hori_btn_map hori_map_default[HB_COUNT] = {
	[HB_TRIGGER]      = { 14, -1 },
	[HB_FIRE_C]       = {  0, -1 },
	[HB_LAUNCH]       = { 13, -1 },
	[HB_A]            = { -1,  0 },
	[HB_B]            = { -1,  1 },
	[HB_HAT]          = {  2, -1 },
	[HB_SW1]          = { 27, -1 },
	[HB_D]            = {  1, -1 },
	[HB_ST]           = {  3, -1 },
	[HB_DPAD3_TOP]    = { 21, -1 },
	[HB_DPAD3_RIGHT]  = { 20, -1 },
	[HB_DPAD3_LEFT]   = { 22, -1 },
	[HB_DPAD1_TOP]    = {  4, -1 },
	[HB_DPAD1_RIGHT]  = {  5, -1 },
	[HB_DPAD1_BOTTOM] = {  6, -1 },
	[HB_DPAD1_LEFT]   = {  7, -1 },
	[HB_DPAD2_TOP]    = { 28, -1 },
	[HB_DPAD2_RIGHT]  = { 29, -1 },
	[HB_DPAD2_BOTTOM] = { 30, -1 },
	[HB_DPAD2_LEFT]   = { 31, -1 },
};

/* Axes that spring back to a centre (the rest have a low and a high end). */
const int hori_axis_centered[AX_COUNT] = { 1, 1, 0, 1, 1, 1, 0, 0 };

/* hori.ini keys */
static const char *const hori_axis_key[AX_COUNT] = {
	"StickX", "StickY", "Throttle", "Rudder", "HatX", "HatY", "APressure", "BPressure",
};

/* vJoyInterface.dll, loaded at runtime */
static BOOL (*vj_Enabled)(void);
static enum vjd_stat (*vj_GetStatus)(UINT);
static BOOL (*vj_Acquire)(UINT);
static VOID (*vj_Relinquish)(UINT);
static BOOL (*vj_Reset)(UINT);
static BOOL (*vj_SetAxis)(LONG, UINT, UINT);
static BOOL (*vj_SetBtn)(BOOL, UINT, UCHAR);
static BOOL (*vj_SetContPov)(DWORD, UINT, UCHAR);
static BOOL (*vj_SetDiscPov)(int, UINT, UCHAR);
static int (*vj_GetButtonNumber)(UINT);
static int (*vj_GetContPovNumber)(UINT);
static int (*vj_GetDiscPovNumber)(UINT);
static BOOL (*vj_GetAxisExist)(UINT, UINT);

/* hfsd (and the Linux driver) send the button requests to recipient "endpoint 1".
 * WinUSB validates that and may refuse it, while the stick likely ignores the
 * recipient, so try other addressings until one answers. */
static const struct { uint8_t recipient; uint16_t index; const char *name; } hori_vr_variants[] = {
	{ LIBUSB_RECIPIENT_ENDPOINT,  0x01, "endpoint 0x01" },
	{ LIBUSB_RECIPIENT_ENDPOINT,  0x81, "endpoint 0x81" },
	{ LIBUSB_RECIPIENT_DEVICE,    0x01, "device, index 1" },
	{ LIBUSB_RECIPIENT_DEVICE,    0x00, "device, index 0" },
	{ LIBUSB_RECIPIENT_INTERFACE, 0x00, "interface 0" },
	{ LIBUSB_RECIPIENT_OTHER,     0x01, "other, index 1" },
};
#define HORI_VR_VARIANTS (int)(sizeof(hori_vr_variants) / sizeof(hori_vr_variants[0]))

int opt_mode = HORI_MODE_BUTTONS;
int opt_threshold = 20;
int opt_debug = 0;
UINT opt_vjoy_id = 1;
static int opt_threshold_set = 0;

int vj_ok = 0;
int vj_buttons = 0;
int vj_cont_povs = 0;
int vj_disc_povs = 0;
LONG vj_sent = 0, vj_failed = 0;
char vj_error[200] = "";

volatile LONG hori_quit = 0;

CRITICAL_SECTION hori_lock;
char hori_log_lines[HORI_LOG_LINES][160];
int hori_log_count = 0;
static char hori_log_partial[440];

struct hori_instance hori_inst;
int hori_connected = 0;
int hori_ir_rate[4];

struct hori_cal hori_cal[AX_COUNT];
struct hori_btn_map hori_map[HB_COUNT];

void (*hori_log_hook)(const char *line) = NULL;
void (*hori_sample_hook)(struct hori_instance *inst) = NULL;

/* Log lines go to stderr, the last few lines shown in the test window, and the log hook. */
void hori_log(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fputs(buf, stderr);

	EnterCriticalSection(&hori_lock);
	for( char *p = buf; *p; p++ ) {
		size_t len = strlen(hori_log_partial);
		if( '\n' == *p ) {
			snprintf(hori_log_lines[hori_log_count++ % HORI_LOG_LINES], sizeof(hori_log_lines[0]), "%s", hori_log_partial);
			if( hori_log_hook )
				hori_log_hook(hori_log_partial);
			hori_log_partial[0] = 0;
		} else if( len + 1 < sizeof(hori_log_partial) ) {
			hori_log_partial[len] = *p;
			hori_log_partial[len + 1] = 0;
		}
	}
	LeaveCriticalSection(&hori_lock);
}

/* Like hori_log, but also remembered as the reason vJoy isn't usable. */
#define hori_vj_error(...) do { snprintf(vj_error, sizeof(vj_error), __VA_ARGS__); hori_log("%s\n", vj_error); } while( 0 )

const char *hori_vr_variant_name(int variant)
{
	if( variant < 0 || variant >= HORI_VR_VARIANTS )
		return "none";
	return hori_vr_variants[variant].name;
}

static BOOL WINAPI hori_ctrl_handler(DWORD type)
{
	InterlockedExchange(&hori_quit, 1);
	return TRUE;
}

/* ---- settings file (hori.ini, next to the exe) ---- */

void hori_ini_path(char *out, size_t size)
{
	if( !GetModuleFileNameA(NULL, out, (DWORD)size) ) {
		snprintf(out, size, "hori.ini");
		return;
	}
	char *slash = strrchr(out, '\\');
	if( slash )
		snprintf(slash + 1, size - (slash + 1 - out), "hori.ini");
}

void hori_cal_load(void)
{
	char path[MAX_PATH], buf[128];
	hori_ini_path(path, sizeof(path));

	for( int ax=0; ax < AX_COUNT; ax++ ) {
		struct hori_cal c;
		GetPrivateProfileStringA("Calibration", hori_axis_key[ax], "", buf, sizeof(buf), path);
		if( 6 == sscanf(buf, "%d,%d,%d,%d,%d,%d", &c.enabled, &c.center, &c.min, &c.max, &c.deadzone, &c.edge)
				&& c.min >= 0 && c.max <= 255 && c.min < c.max )
			hori_cal[ax] = c;
	}

	int t = GetPrivateProfileIntA("Calibration", "Threshold", -1, path);
	if( t >= 0 && t <= 254 && !opt_threshold_set )
		opt_threshold = t;

	memcpy(hori_map, hori_map_default, sizeof(hori_map));
	for( int i=0; i < HB_COUNT; i++ ) {
		struct hori_btn_map m;
		GetPrivateProfileStringA("Buttons", hori_btn_key[i], "", buf, sizeof(buf), path);
		if( 2 == sscanf(buf, "%d,%d", &m.bit, &m.chan)
				&& m.bit >= -1 && m.bit < HORI_WORD_BITS && (m.bit < 0 || !((1ull << m.bit) & HORI_MODE_BITS))
				&& m.chan >= -1 && m.chan < 2 )
			hori_map[i] = m;
	}
}

void hori_map_save(void)
{
	char path[MAX_PATH], buf[32];
	hori_ini_path(path, sizeof(path));
	for( int i=0; i < HB_COUNT; i++ ) {
		snprintf(buf, sizeof(buf), "%d,%d", hori_map[i].bit, hori_map[i].chan);
		WritePrivateProfileStringA("Buttons", hori_btn_key[i], buf, path);
	}
	// D-PAD 3 turned out to have no down position; drop the key older builds wrote.
	WritePrivateProfileStringA("Buttons", "Pad3Down", NULL, path);
}

uint64_t hori_button_word(const struct hori_instance *inst)
{
	uint8_t b[4];
	memcpy(b, &inst->state_vr00, 2);
	memcpy(b + 2, &inst->state_vr01, 2);
	return b[0] | (uint64_t)b[1] << 8 | (uint64_t)b[2] << 16 | (uint64_t)b[3] << 24
		| (uint64_t)inst->state_vr03 << 32;
}

/* Whether the data holding this bit of the button word has been received. */
int hori_word_bit_have(const struct hori_instance *inst, int bit)
{
	if( bit < 16 ) return inst->have_vr00;
	if( bit < 32 ) return inst->have_vr01;
	return inst->have_vr03;
}

/* Request 0x03 rests at 0; the others rest at 1 and drop to 0 when pressed. */
static inline int hori_word_bit_down(uint64_t word, int bit)
{
	int idle = bit < 32;
	return (int)((word >> bit) & 1) != idle;
}

/* "Button A pressure", or whichever button the channel belongs to. */
const char *hori_axis_label(int ax)
{
	static char names[2][48];
	if( AX_A != ax && AX_B != ax )
		return hori_axis_name[ax];

	int chan = ax - AX_A;
	for( int i=0; i < HB_COUNT; i++ )
		if( hori_map[i].chan == chan ) {
			snprintf(names[chan], sizeof(names[chan]), "%s pressure", hori_btn_name[i]);
			return names[chan];
		}
	return hori_axis_name[ax];
}

void hori_cal_save(void)
{
	char path[MAX_PATH], buf[128];
	hori_ini_path(path, sizeof(path));

	for( int ax=0; ax < AX_COUNT; ax++ ) {
		const struct hori_cal *c = &hori_cal[ax];
		snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d", c->enabled, c->center, c->min, c->max, c->deadzone, c->edge);
		WritePrivateProfileStringA("Calibration", hori_axis_key[ax], buf, path);
	}
	snprintf(buf, sizeof(buf), "%d", opt_threshold);
	WritePrivateProfileStringA("Calibration", "Threshold", buf, path);
}

/* Apply dead zones and range to a 0-255 value. */
int hori_cal_apply(const struct hori_cal *c, int v)
{
	int out;

	if( !c->enabled )
		return v;

	if( c->center >= 0 ) {
		int lo = c->min + c->edge, hi = c->max - c->edge;
		int dlo = c->center - c->deadzone, dhi = c->center + c->deadzone;
		if( v >= dlo && v <= dhi )
			out = 128;
		else if( v > dhi )
			out = hi > dhi ? 128 + (v - dhi) * 127 / (hi - dhi) : 255;
		else
			out = dlo > lo ? 128 - (dlo - v) * 128 / (dlo - lo) : 0;
	} else {
		int lo = c->min + c->deadzone, hi = c->max - c->edge;
		if( hi <= lo )
			return v <= lo ? 0 : 255;
		out = (v - lo) * 255 / (hi - lo);
	}

	if( out < 0 ) out = 0;
	if( out > 255 ) out = 255;
	return out;
}

/* ---- vJoy ---- */

static int hori_vjoy_load(void)
{
	HMODULE dll = NULL;
	char path[MAX_PATH];

	// Only load it from vJoy's own install folder, or next to hori.exe.
	const char *pf = getenv("ProgramFiles");
	if( pf ) {
		snprintf(path, sizeof(path), "%s\\vJoy\\x64\\vJoyInterface.dll", pf);
		dll = LoadLibraryA(path);
	}
	if( !dll && GetModuleFileNameA(NULL, path, sizeof(path)) ) {
		char *slash = strrchr(path, '\\');
		if( slash ) {
			strcpy(slash + 1, "vJoyInterface.dll");
			dll = LoadLibraryA(path);
		}
	}
	if( !dll ) {
		hori_vj_error("Could not load vJoyInterface.dll. Is vJoy installed?");
		return -1;
	}

#define VJ_LOAD(var, name) if( !(var = (__typeof__(var))(void (*)(void))GetProcAddress(dll, name)) ) { hori_vj_error("vJoyInterface.dll is missing %s", name); return -1; }
	VJ_LOAD(vj_Enabled, "vJoyEnabled");
	VJ_LOAD(vj_GetStatus, "GetVJDStatus");
	VJ_LOAD(vj_Acquire, "AcquireVJD");
	VJ_LOAD(vj_Relinquish, "RelinquishVJD");
	VJ_LOAD(vj_Reset, "ResetVJD");
	VJ_LOAD(vj_SetAxis, "SetAxis");
	VJ_LOAD(vj_SetBtn, "SetBtn");
	VJ_LOAD(vj_SetContPov, "SetContPov");
	VJ_LOAD(vj_SetDiscPov, "SetDiscPov");
	VJ_LOAD(vj_GetButtonNumber, "GetVJDButtonNumber");
	VJ_LOAD(vj_GetContPovNumber, "GetVJDContPovNumber");
	VJ_LOAD(vj_GetDiscPovNumber, "GetVJDDiscPovNumber");
	VJ_LOAD(vj_GetAxisExist, "GetVJDAxisExist");
#undef VJ_LOAD

	return 0;
}

static int hori_vjoy_init(void)
{
	if( 0 != hori_vjoy_load() )
		return -1;

	if( !vj_Enabled() ) {
		hori_vj_error("vJoy is installed but not enabled. Open \"Configure vJoy\" and enable it.");
		return -1;
	}

	enum vjd_stat st = vj_GetStatus(opt_vjoy_id);
	if( VJD_STAT_BUSY == st ) {
		hori_vj_error("vJoy device %u is in use by another program (another hori.exe?).", opt_vjoy_id);
		return -1;
	}
	if( VJD_STAT_MISS == st || VJD_STAT_UNKN == st ) {
		hori_vj_error("vJoy device %u doesn't exist. Create it in \"Configure vJoy\".", opt_vjoy_id);
		return -1;
	}
	if( VJD_STAT_OWN != st && !vj_Acquire(opt_vjoy_id) ) {
		hori_vj_error("Could not acquire vJoy device %u.", opt_vjoy_id);
		return -1;
	}
	vj_ok = 1;
	vj_Reset(opt_vjoy_id);

	vj_buttons = vj_GetButtonNumber(opt_vjoy_id);
	vj_cont_povs = vj_GetContPovNumber(opt_vjoy_id);
	vj_disc_povs = vj_GetDiscPovNumber(opt_vjoy_id);

	int need = (HORI_MODE_SHIFT == opt_mode) ? 2 * HORI_LAYER_STRIDE + 23 : 23;
	if( vj_buttons < need )
		hori_log("Warning: vJoy device %u has %d buttons, %d are needed for this mode. Raise it in \"Configure vJoy\".\n",
				opt_vjoy_id, vj_buttons, need);

	static const struct { UINT id; const char *name; } axes[] = {
		{ HID_USAGE_X, "X" }, { HID_USAGE_Y, "Y" }, { HID_USAGE_Z, "Z" },
		{ HID_USAGE_RX, "Rx" }, { HID_USAGE_RY, "Ry" }, { HID_USAGE_RZ, "Rz" },
		{ HID_USAGE_SL0, "Slider" }, { HID_USAGE_SL1, "Dial/Slider2" },
	};
	for( size_t i=0; i < sizeof(axes) / sizeof(axes[0]); i++ )
		if( !vj_GetAxisExist(opt_vjoy_id, axes[i].id) )
			hori_log("Warning: vJoy device %u has no %s axis; enable it in \"Configure vJoy\".\n",
					opt_vjoy_id, axes[i].name);

	hori_log("vJoy device %u: %d buttons, %d continuous + %d 4-way POVs.\n",
			opt_vjoy_id, vj_buttons, vj_cont_povs, vj_disc_povs);

	if( vj_cont_povs + vj_disc_povs < 2 )
		hori_log("Note: fewer than 2 POV hats on vJoy device %u, so D-PAD 1/2 are buttons 16-23.\n", opt_vjoy_id);
	if( vj_cont_povs + vj_disc_povs < 3 )
		hori_log("Note: D-PAD 3 is buttons 13/14/15; set 3 POV hats in \"Configure vJoy\" to make it POV 3.\n");

	return 0;
}

static void hori_vj_count(BOOL ok)
{
	if( ok )
		vj_sent++;
	else
		vj_failed++;
}

static void hori_set_btn(int num, int val)
{
	if( !vj_ok )
		return;
	if( num > vj_buttons ) {
		if( opt_debug )
			hori_log("vJoy: button %d skipped, the device only has %d buttons\n", num, vj_buttons);
		return;
	}
	BOOL ok = vj_SetBtn(val, opt_vjoy_id, (UCHAR)num);
	hori_vj_count(ok);
	if( opt_debug )
		hori_log("vJoy: button %d = %d%s\n", num, val, ok ? "" : "  FAILED");
}

/* 0-255 to vJoy's 1-32768 axis range */
static void hori_set_axis(struct hori_instance *inst, UINT axis, int val)
{
	long v = 1 + (long)val * 32767 / 255;
	if( !vj_ok || inst->axis_last[axis - HID_USAGE_X] == v )
		return;
	inst->axis_last[axis - HID_USAGE_X] = v;
	BOOL ok = vj_SetAxis(v, opt_vjoy_id, axis);
	hori_vj_count(ok);
	if( !ok && opt_debug )
		hori_log("vJoy: axis %02x = %ld FAILED\n", axis, v);
}

/* D-pad bits to a POV hat, in hundredths of a degree (diagonals on continuous POVs). */
static void hori_set_pov(struct hori_instance *inst, int n, int up, int right, int down, int left)
{
	DWORD v = (DWORD)-1;
	int x = right - left, y = down - up;

	if( !vj_ok )
		return;

	if( vj_cont_povs > n ) {
		static const int angle[3][3] = {
			{ 31500,     0,  4500 },   /* up-left, up, up-right */
			{ 27000,    -1,  9000 },   /* left, centre, right */
			{ 22500, 18000, 13500 },   /* down-left, down, down-right */
		};
		v = (DWORD)angle[y + 1][x + 1];
		if( inst->pov_last[n] == v )
			return;
		inst->pov_last[n] = v;
		hori_vj_count(vj_SetContPov(v, opt_vjoy_id, (UCHAR)(n + 1)));
	} else {
		int d = -1;
		if( y < 0 ) d = 0;
		else if( x > 0 ) d = 1;
		else if( y > 0 ) d = 2;
		else if( x < 0 ) d = 3;
		if( inst->pov_last[n] == (DWORD)d )
			return;
		inst->pov_last[n] = (DWORD)d;
		hori_vj_count(vj_SetDiscPov(d, opt_vjoy_id, (UCHAR)(n + 1)));
	}
}

/* ---- reading the stick ---- */

/* A/B raw values idle at ~0xff and drop as they're pressed.
 * Scale to 0 (released) - 255 (fully pressed), same as hfsd. */
int hori_pressure(uint8_t raw)
{
	int v = ((0xff - raw) - 0x26) * 0xff / 0xc2;
	if( v < 0 ) v = 0;
	if( v > 0xff ) v = 0xff;
	return v;
}

static inline int hori_pressure_btn(int was_pressed, int pressure)
{
	if( was_pressed )
		return pressure + 8 > opt_threshold;
	return pressure > opt_threshold;
}

static int hori_poll_ir(struct hori_instance *inst)
{
	int transferred;
	struct hori_input_ir ir;

	int err = libusb_interrupt_transfer(inst->usbdev_handle, 0x81, (unsigned char *)&ir, sizeof(ir), &transferred, 50);

	if( LIBUSB_ERROR_TIMEOUT == err ) {
		inst->ir_timeout++;
		return 0;
	}

	if( LIBUSB_ERROR_NO_DEVICE == err )
		return -1;

	if( 0 != err) {
		static int ir_fail_count = 0;
		inst->ir_err++;
		inst->ir_last_err = err;
		if( 0 == ir_fail_count++ % 200 )
			hori_log("hori_poll_ir: libusb_interrupt_transfer failed: %d: %s\n", err, libusb_strerror(err));
		if( LIBUSB_ERROR_PIPE == err )
			libusb_clear_halt(inst->usbdev_handle, 0x81);
		return 0;
	}

	if( transferred != sizeof(ir) ) {
		if( opt_debug && inst->ir_short < 5 ) {
			hori_log("hori_poll_ir: Expected %u bytes, received %d:",
					(unsigned)sizeof(ir), transferred);
			for( int i=0; i < transferred; i++ )
				hori_log(" %02x", ((uint8_t *)&ir)[i]);
			hori_log("\n");
		}
		inst->ir_short++;
		return 0;
	}

	inst->ir_ok++;
	inst->state_ir = ir;
	inst->have_ir = 1;
	return 0;
}

static int hori_vr_transfer(struct hori_instance *inst, int variant, uint8_t request, uint8_t *buf, uint16_t size)
{
	return libusb_control_transfer(inst->usbdev_handle,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | hori_vr_variants[variant].recipient,
			request, 0, hori_vr_variants[variant].index, buf, size, 200);
}

/* Find an addressing of the button requests that this setup accepts. */
static void hori_vr_detect(struct hori_instance *inst)
{
	uint8_t vr[2];
	for( int v=0; v < HORI_VR_VARIANTS; v++ ) {
		int err = hori_vr_transfer(inst, v, HORI_POLL_VR0, vr, sizeof(vr));
		if( opt_debug )
			hori_log("Button request via %s: %d (%s)\n", hori_vr_variants[v].name,
					err, err < 0 ? libusb_strerror(err) : "ok");
		if( (int)sizeof(vr) == err ) {
			inst->vr_variant = v;
			if( v != 0 )
				hori_log("Buttons: using %s addressing.\n", hori_vr_variants[v].name);
			return;
		}
		if( LIBUSB_ERROR_NO_DEVICE == err )
			return;
	}
	inst->vr_variant = -1;
	hori_log("The stick doesn't answer the button requests in any form tried, so only the axes\n"
			"will work. Run \"hori.exe -d\" and send the output to whoever is helping you.\n");
}

/* -P: read-only look at which other vendor requests answer, to hunt for unknown controls. */
static void hori_vr_probe(struct hori_instance *inst)
{
	uint8_t buf[16];
	if( inst->vr_variant < 0 )
		return;
	for( int req=0; req < 0x20; req++ ) {
		memset(buf, 0, sizeof(buf));
		int err = hori_vr_transfer(inst, inst->vr_variant, req, buf, sizeof(buf));
		if( err < 0 ) {
			hori_log("probe: request %02x: %s\n", req, libusb_strerror(err));
			continue;
		}
		hori_log("probe: request %02x: %d bytes:", req, err);
		for( int i=0; i < err; i++ )
			hori_log(" %02x", buf[i]);
		hori_log("\n");
	}
}

static int opt_probe = 0;

static int hori_poll_vr(struct hori_instance *inst, uint8_t request, void *out, uint16_t size, int *have)
{
	uint8_t vr[2];

	if( inst->vr_variant < 0 )
		return 0;

	int err = hori_vr_transfer(inst, inst->vr_variant, request, vr, size);

	if( LIBUSB_ERROR_PIPE == err || LIBUSB_ERROR_TIMEOUT == err )
		return 0;

	if( LIBUSB_ERROR_NO_DEVICE == err )
		return -1;

	// Anything else: report it once in a while, but keep going; only a real
	// unplug (above) or a long run of failures counts as a disconnect.
	if( 0 > err) {
		if( 0 == inst->vr_fail_count % 200 )
			hori_log("hori_poll_vr: request %02x: libusb_control_transfer failed: %d: %s\n", request, err, libusb_strerror(err));
		if( ++inst->vr_fail_count >= 1000 )
			return -1;
		return 0;
	}
	inst->vr_fail_count = 0;

	if( err != size ) {
		if( opt_debug )
			hori_log("hori_poll_vr: request %02x: Expected %u bytes, received %d bytes.\n",
					request, size, err);
		return 0;
	}

	memcpy(out, vr, size);
	*have = 1;
	return 0;
}

static void hori_debug_dump(struct hori_instance *inst)
{
	uint8_t raw[13];
	memcpy(raw, &inst->state_ir, 8);
	memcpy(raw + 8, &inst->state_vr00, 2);
	memcpy(raw + 10, &inst->state_vr01, 2);
	raw[12] = inst->state_vr03;
	if( 0 == memcmp(raw, inst->dbg_raw, sizeof(raw)) )
		return;
	memcpy(inst->dbg_raw, raw, sizeof(raw));

	hori_log("ir:");
	for( int i=0; i < 8; i++ )
		hori_log(" %02x", raw[i]);
	hori_log("  vr00: %02x %02x  vr01: %02x %02x  vr03: %02x\n", raw[8], raw[9], raw[10], raw[11], raw[12]);
}

static void hori_report(struct hori_instance *inst)
{
	int i;
	uint8_t btn[HB_COUNT];
	memcpy(btn, inst->btn, sizeof(btn));

	if( inst->have_ir ) {
		struct hori_input_ir *ir = &inst->state_ir;
		int *raw = inst->axis_raw, *out = inst->axis_out;
		raw[AX_X] = ir->pos_x;
		raw[AX_Y] = ir->pos_y;
		raw[AX_THR] = ir->throttle;
		raw[AX_RUD] = ir->rudder;
		raw[AX_HATX] = ir->hat_x;
		raw[AX_HATY] = ir->hat_y;
		raw[AX_A] = hori_pressure(ir->btn_a);
		raw[AX_B] = hori_pressure(ir->btn_b);
		for( i=0; i < AX_COUNT; i++ )
			out[i] = hori_cal_apply(&hori_cal[i], raw[i]);

		hori_set_axis(inst, HID_USAGE_X, out[AX_X]);
		hori_set_axis(inst, HID_USAGE_Y, out[AX_Y]);
		hori_set_axis(inst, HID_USAGE_Z, 0xff - out[AX_THR]);   // Windows convention: forward = high
		hori_set_axis(inst, HID_USAGE_RZ, out[AX_RUD]);
		hori_set_axis(inst, HID_USAGE_RX, out[AX_HATX]);
		hori_set_axis(inst, HID_USAGE_RY, out[AX_HATY]);
		hori_set_axis(inst, HID_USAGE_SL0, out[AX_A]);
		hori_set_axis(inst, HID_USAGE_SL1, out[AX_B]);
	}

	// Each button comes from its bit and/or its pressure channel, per hori_map.
	uint64_t word = hori_button_word(inst);
	for( i=0; i < HB_COUNT; i++ ) {
		const struct hori_btn_map *m = &hori_map[i];
		int known = 0, down = 0;
		if( m->bit >= 0 && hori_word_bit_have(inst, m->bit) ) {
			known = 1;
			down |= hori_word_bit_down(word, m->bit);
		}
		if( m->chan >= 0 && inst->have_ir ) {
			known = 1;
			down |= hori_pressure_btn(inst->btn[i], inst->axis_raw[AX_A + m->chan]);
		}
		if( known )
			btn[i] = down;
	}

	if( inst->have_vr01 ) {
		struct hori_input_vr_01 *vr = &inst->state_vr01;

		// 2 = M1, 1 = M2, 3 = M3. 0 shows up briefly while the switch is
		// moving; ignore it rather than reporting a bogus mode.
		static const int mode_map[4] = { -1, 1, 0, 2 };
		int mode = mode_map[vr->mode_select];
		if( mode >= 0 && mode != inst->mode && HORI_MODE_OFF != opt_mode ) {
			if( HORI_MODE_BUTTONS == opt_mode ) {
				if( inst->mode >= 0 )
					hori_set_btn(HORI_MODE_BTN_BASE + inst->mode, 0);
				hori_set_btn(HORI_MODE_BTN_BASE + mode, 1);
			} else if( HORI_MODE_AXIS == opt_mode ) {
				hori_set_axis(inst, HID_USAGE_WHL, mode * 127);
			}
			hori_log("Mode M%d\n", mode + 1);
			inst->mode = mode;
		}
	}

	// D-PAD 1 and 2 go on POV 1 and 2, D-PAD 3 on POV 3, when vJoy has that many.
	int povs = vj_cont_povs + vj_disc_povs;
	int on_pov[HB_COUNT] = { 0 };
	if( povs >= 2 ) {
		static const int pov_pad[2] = { HB_DPAD1_TOP, HB_DPAD2_TOP };
		for( int p=0; p < 2; p++ ) {
			int f = pov_pad[p];
			hori_set_pov(inst, p, btn[f], btn[f + 1], btn[f + 2], btn[f + 3]);
			for( int d=0; d < 4; d++ )
				on_pov[f + d] = 1;
		}
	}
	if( povs >= 3 ) {
		hori_set_pov(inst, 2, btn[HB_DPAD3_TOP], btn[HB_DPAD3_RIGHT], 0, btn[HB_DPAD3_LEFT]);
		on_pov[HB_DPAD3_TOP] = on_pov[HB_DPAD3_RIGHT] = on_pov[HB_DPAD3_LEFT] = 1;
	}

	int layer = (HORI_MODE_SHIFT == opt_mode && inst->mode >= 0) ? inst->mode : 0;

	// On a layer change, let go of everything held on the old layer.
	if( layer != inst->layer ) {
		for( i=0; i < HB_COUNT; i++ ) {
			if( !inst->btn[i] || on_pov[i] )
				continue;
			hori_set_btn(hori_btn_num[i] + inst->layer * HORI_LAYER_STRIDE, 0);
			inst->btn[i] = 0;
		}
		inst->layer = layer;
	}

	for( i=0; i < HB_COUNT; i++ ) {
		if( btn[i] == inst->btn[i] )
			continue;
		if( !on_pov[i] )
			hori_set_btn(hori_btn_num[i] + layer * HORI_LAYER_STRIDE, btn[i]);
		inst->btn[i] = btn[i];
	}
}

static libusb_device_handle *hori_usb_open(void)
{
	libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, HORI_VENDOR_ID, HORI_PRODUCT_ID);
	if( !h )
		return NULL;

	int err = libusb_claim_interface(h, 0);
	if( LIBUSB_SUCCESS != err ) {
		hori_log("Cannot claim the stick's USB interface: %s\n", libusb_strerror(err));
		libusb_close(h);
		return NULL;
	}
	libusb_set_interface_alt_setting(h, 0, 0);

	if( opt_debug ) {
		struct libusb_config_descriptor *cfg;
		if( LIBUSB_SUCCESS == libusb_get_active_config_descriptor(libusb_get_device(h), &cfg) ) {
			for( int i=0; i < cfg->bNumInterfaces; i++ ) {
				const struct libusb_interface_descriptor *id = &cfg->interface[i].altsetting[0];
				hori_log("USB interface %d: class %02x, %d endpoint(s)\n",
						id->bInterfaceNumber, id->bInterfaceClass, id->bNumEndpoints);
				for( int e=0; e < id->bNumEndpoints; e++ )
					hori_log("  endpoint %02x attr %02x maxpkt %d interval %d\n",
							id->endpoint[e].bEndpointAddress, id->endpoint[e].bmAttributes,
							id->endpoint[e].wMaxPacketSize, id->endpoint[e].bInterval);
			}
			libusb_free_config_descriptor(cfg);
		}
	}
	return h;
}

/* Tell the user why the stick can't be opened, if it's plugged in at all. */
static void hori_usb_diagnose(void)
{
	libusb_device **list;
	ssize_t n = libusb_get_device_list(NULL, &list);
	for( ssize_t i=0; i < n; i++ ) {
		struct libusb_device_descriptor desc;
		if( LIBUSB_SUCCESS != libusb_get_device_descriptor(list[i], &desc) )
			continue;
		if( HORI_VENDOR_ID != desc.idVendor || HORI_PRODUCT_ID != desc.idProduct )
			continue;

		libusb_device_handle *h;
		int err = libusb_open(list[i], &h);
		if( LIBUSB_SUCCESS == err )
			libusb_close(h);
		else
			hori_log("The stick is plugged in but can't be opened (%s).\n"
					"Install the WinUSB driver for it with Zadig (see README-Windows.txt).\n",
					libusb_strerror(err));
	}
	libusb_free_device_list(list, 1);
}

static DWORD WINAPI hori_run(LPVOID unused)
{
	struct hori_instance *inst = &hori_inst;
	int waiting_msg = 0;

	while( !hori_quit )
	{
		EnterCriticalSection(&hori_lock);
		memset(inst, 0, sizeof(*inst));
		inst->mode = -1;
		for( int i=0; i < 16; i++ ) inst->axis_last[i] = -1;
		inst->pov_last[0] = inst->pov_last[1] = inst->pov_last[2] = (DWORD)-2;
		memset(hori_ir_rate, 0, sizeof(hori_ir_rate));
		LeaveCriticalSection(&hori_lock);

		libusb_device_handle *h = hori_usb_open();
		if( !h ) {
			if( !waiting_msg ) {
				hori_log("Waiting for the stick...\n");
				hori_usb_diagnose();
				waiting_msg = 1;
			}
			for( int i=0; i < 20 && !hori_quit; i++ )
				Sleep(100);
			continue;
		}
		waiting_msg = 0;

		EnterCriticalSection(&hori_lock);
		inst->usbdev_handle = h;
		inst->ir_stat_time = GetTickCount();
		hori_connected = 1;
		hori_log("Stick connected.\n");
		hori_vr_detect(inst);
		if( opt_probe ) {
			hori_vr_probe(inst);
			InterlockedExchange(&hori_quit, 1);
		}
		LeaveCriticalSection(&hori_lock);

		while( !hori_quit )
		{
			EnterCriticalSection(&hori_lock);
			int err = hori_poll_ir(inst);
			if( 0 == err )
				err = hori_poll_vr(inst, HORI_POLL_VR0, &inst->state_vr00, sizeof(inst->state_vr00), &inst->have_vr00);
			if( 0 == err )
				err = hori_poll_vr(inst, HORI_POLL_VR1, &inst->state_vr01, sizeof(inst->state_vr01), &inst->have_vr01);
			if( 0 == err )
				err = hori_poll_vr(inst, HORI_POLL_VR3, &inst->state_vr03, sizeof(inst->state_vr03), &inst->have_vr03);
			if( 0 != err ) {
				LeaveCriticalSection(&hori_lock);
				break;
			}

			DWORD now = GetTickCount();
			if( now - inst->ir_stat_time >= 1000 ) {
				hori_ir_rate[0] = inst->ir_ok;
				hori_ir_rate[1] = inst->ir_short;
				hori_ir_rate[2] = inst->ir_timeout;
				hori_ir_rate[3] = inst->ir_err;
				if( opt_debug ) {
					hori_log("stick axes, last 1s: %d full reads, %d short, %d timeouts, %d errors",
							inst->ir_ok, inst->ir_short, inst->ir_timeout, inst->ir_err);
					if( inst->ir_err )
						hori_log(" (last: %s)", libusb_strerror(inst->ir_last_err));
					hori_log("\n");
				}
				inst->ir_ok = inst->ir_short = inst->ir_timeout = inst->ir_err = 0;
				inst->ir_stat_time = now;
			}

			if( opt_debug )
				hori_debug_dump(inst);
			hori_report(inst);
			if( hori_sample_hook )
				hori_sample_hook(inst);
			LeaveCriticalSection(&hori_lock);
			Sleep(5);
		}

		EnterCriticalSection(&hori_lock);
		hori_connected = 0;
		LeaveCriticalSection(&hori_lock);

		libusb_release_interface(h, 0);
		libusb_close(h);
		if( vj_ok )
			vj_Reset(opt_vjoy_id);
		if( !hori_quit )
			hori_log("Stick disconnected.\n");
	}

	return 0;
}

static void hori_usage(const char *argv0)
{
	hori_log("Usage: %s [options]\n"
		"  -m MODE   How to expose the M1/M2/M3 switch (default: buttons)\n"
		"              off      ignore it\n"
		"              buttons  buttons 10/11/12, the selected one is held down\n"
		"              axis     the Wheel axis: low = M1, middle = M2, high = M3\n"
		"              shift    every button number goes up by 32 in M2 and 64 in M3\n"
		"  -t N      A/B press threshold, 0-254 on the 0-255 pressure scale\n"
		"            (default: from the setup wizard, or 20)\n"
		"  -n ID     vJoy device number to use (default: 1)\n"
		"  -g        Open the test window (shows every control live)\n"
		"  -d        Debug: print raw reports as they change\n"
		"  -P        Debug: read vendor requests 0x00-0x1f once, print them and exit\n"
		"  -h        This help\n"
		"Dead zones come from hori.ini, written by the setup wizard in the test window.\n", argv0);
}

#ifdef HORI_GUI_DEFAULT
static int opt_gui = 1;
#else
static int opt_gui = 0;
#endif

int main(int argc, char **argv)
{
	int c;

	InitializeCriticalSection(&hori_lock);

	while( -1 != (c = getopt(argc, argv, "m:t:n:gdPh")) )
	{
		switch( c ) {
		case 'm':
			if( 0 == strcmp(optarg, "off") ) opt_mode = HORI_MODE_OFF;
			else if( 0 == strcmp(optarg, "buttons") ) opt_mode = HORI_MODE_BUTTONS;
			else if( 0 == strcmp(optarg, "axis") ) opt_mode = HORI_MODE_AXIS;
			else if( 0 == strcmp(optarg, "shift") ) opt_mode = HORI_MODE_SHIFT;
			else { hori_usage(argv[0]); return EXIT_FAILURE; }
			break;
		case 't':
			opt_threshold = atoi(optarg);
			opt_threshold_set = 1;
			if( opt_threshold < 0 || opt_threshold > 254 ) { hori_usage(argv[0]); return EXIT_FAILURE; }
			break;
		case 'n':
			opt_vjoy_id = (UINT)atoi(optarg);
			if( opt_vjoy_id < 1 || opt_vjoy_id > 16 ) { hori_usage(argv[0]); return EXIT_FAILURE; }
			break;
		case 'g': opt_gui = 1; break;
		case 'd': opt_debug = 1; break;
		case 'P': opt_probe = 1; break;
		case 'h': hori_usage(argv[0]); return EXIT_SUCCESS;
		default: hori_usage(argv[0]); return EXIT_FAILURE;
		}
	}

	if( opt_gui )
		hori_gui_init();

	SetConsoleCtrlHandler(hori_ctrl_handler, TRUE);
	hori_cal_load();

	// In the test window a missing vJoy is shown, not fatal: the stick side can still be tested.
	if( 0 != hori_vjoy_init() && !opt_gui ) {
		hori_log("\nPress Enter to exit.\n");
		getchar();
		return EXIT_FAILURE;
	}

	if( LIBUSB_SUCCESS != libusb_init(NULL) ) {
		hori_log("libusb_init failed.\n");
		if( opt_gui )
			MessageBoxA(NULL, "libusb_init failed.", "Flightstick 2 test", MB_ICONERROR);
		return EXIT_FAILURE;
	}

	hori_log("Mitsubishi HORI/Namco Flightstick 2 driver for Windows (vJoy device %u).%s\n",
			opt_vjoy_id, opt_gui ? "" : " Ctrl+C to quit.");

	timeBeginPeriod(1);

	if( opt_gui ) {
		HANDLE worker = CreateThread(NULL, 0, hori_run, NULL, 0, NULL);
		hori_gui();
		InterlockedExchange(&hori_quit, 1);
		WaitForSingleObject(worker, 5000);
	} else
		hori_run(NULL);

	timeEndPeriod(1);
	if( vj_ok ) {
		vj_Reset(opt_vjoy_id);
		vj_Relinquish(opt_vjoy_id);
	}
	libusb_exit(NULL);
	return 0;
}
