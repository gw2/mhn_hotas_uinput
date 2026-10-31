/*
 * Shared between the Windows driver core (hori_win.c) and the
 * test window / setup wizard (hori_gui.c).
 */
#ifndef HORI_WIN_H
#define HORI_WIN_H

#include <windows.h>
#include <stdint.h>

#include <libusb.h>

#define HORI_VERSION "0.2.0"

struct hori_input_ir {
	uint8_t	pos_x;
	uint8_t	pos_y;

	uint8_t	rudder;
	uint8_t	throttle;

	uint8_t	hat_x;
	uint8_t	hat_y;

	uint8_t	btn_a;
	uint8_t	btn_b;
};

/* input: vendor request 0x00 */
struct hori_input_vr_00 {
	uint8_t fire_c : 1;           /* button fire-c */
	uint8_t button_d : 1;         /* button D */
	uint8_t hat : 1;              /* hat press */
	uint8_t button_st : 1;        /* button ST */

	uint8_t dpad1_top : 1;        /* d-pad 1 top */
	uint8_t dpad1_right : 1;      /* d-pad 1 right */
	uint8_t dpad1_bottom : 1;     /* d-pad 1 bottom */
	uint8_t dpad1_left : 1;       /* d-pad 1 left */

	uint8_t reserved1 : 4;

	uint8_t reserved2 : 1;
	uint8_t launch : 1;           /* button lauch */
	uint8_t trigger : 1;          /* trigger */
	uint8_t reserved3 : 1;
};

/* input: vendor request 0x01 */
struct hori_input_vr_01 {
	uint8_t reserved1 : 4;

	uint8_t dpad3_right : 1;      /* d-pad 3 right */
	uint8_t dpad3_middle : 1;     /* d-pad 3 middle */
	uint8_t dpad3_left : 1;       /* d-pad 3 left */
	uint8_t reserved2 : 1;

	uint8_t mode_select : 2;      /* mode select (M1 - M2 - M3, 2 - 1 - 3) */
	uint8_t reserved3 : 1;
	uint8_t button_sw1 : 1;       /* button sw-1 */

	uint8_t dpad2_top : 1;        /* d-pad 2 top */
	uint8_t dpad2_right : 1;      /* d-pad 2 right */
	uint8_t dpad2_bottom : 1;     /* d-pad 2 bottom */
	uint8_t dpad2_left : 1;       /* d-pad 2 left */
};

enum hori_mode_style {
	HORI_MODE_OFF,
	HORI_MODE_BUTTONS,
	HORI_MODE_AXIS,
	HORI_MODE_SHIFT,
};

/* Logical digital buttons. D-PAD 1/2 are up, right, down, left in that order;
 * D-PAD 3 has only three positions (up, right, left). */
enum {
	HB_TRIGGER, HB_FIRE_C, HB_LAUNCH, HB_A, HB_B, HB_HAT, HB_SW1, HB_D, HB_ST,
	HB_DPAD3_TOP, HB_DPAD3_RIGHT, HB_DPAD3_LEFT,
	HB_DPAD1_TOP, HB_DPAD1_RIGHT, HB_DPAD1_BOTTOM, HB_DPAD1_LEFT,
	HB_DPAD2_TOP, HB_DPAD2_RIGHT, HB_DPAD2_BOTTOM, HB_DPAD2_LEFT,
	HB_COUNT
};

/* Analog inputs. AX_A/AX_B are the two pressure channels (0 = released, 255 = full);
 * which buttons they belong to is in hori_map (A and B by default). */
enum {
	AX_X, AX_Y, AX_THR, AX_RUD, AX_HATX, AX_HATY, AX_A, AX_B,
	AX_COUNT
};

/* Where a button's signal comes from. The button bytes (vr00, vr01, then the
 * one byte of vendor request 0x03) form one 40-bit word; bits 0-31 are active-low. */
struct hori_btn_map {
	int bit;        /* 0-39 in the button word, -1 = none */
	int chan;       /* pressure channel 0/1 (AX_A/AX_B), -1 = none */
};

#define HORI_WORD_BITS  40
#define HORI_MODE_BITS  0x03000000ull   /* the mode switch, not a button */

/* Dead zone / range calibration for one axis, in 0-255 input units. */
struct hori_cal {
	int enabled;    /* 0 = pass the raw value through */
	int center;     /* -1 for one-ended axes (throttle, A/B) */
	int min, max;   /* input range the stick really reaches */
	int deadzone;   /* around the centre; for one-ended axes, at the low end */
	int edge;       /* outer dead zone at the end(s) */
};

struct hori_instance {
	struct hori_input_ir	state_ir;
	struct hori_input_vr_00	state_vr00;
	struct hori_input_vr_01	state_vr01;
	uint8_t			state_vr03;     /* unknown; read in case the dials or D-PAD 3 report here */
	int have_ir, have_vr00, have_vr01, have_vr03;

	uint8_t	dbg_raw[13];

	/* what has been reported to vJoy */
	uint8_t	btn[HB_COUNT];
	int	layer;
	int	mode;           /* 0 = M1, 1 = M2, 2 = M3, -1 = unknown */
	long	axis_last[16];
	DWORD	pov_last[3];

	/* analog values before and after calibration */
	int	axis_raw[AX_COUNT];
	int	axis_out[AX_COUNT];

	/* how the button vendor requests are addressed (see hori_vr_variants) */
	int vr_variant;
	int vr_fail_count;

	/* interrupt read stats, summarised every second */
	int ir_ok, ir_short, ir_timeout, ir_err, ir_last_err;
	DWORD ir_stat_time;

	struct libusb_device_handle *usbdev_handle;
};

extern const char *const hori_btn_name[HB_COUNT];
extern const char *const hori_axis_name[AX_COUNT];
extern const int hori_axis_centered[AX_COUNT];

extern int opt_mode;
extern int opt_threshold;
extern int opt_debug;
extern UINT opt_vjoy_id;

/* Why vJoy is or isn't usable (vj_state). */
enum { VJS_OK, VJS_NOT_INSTALLED, VJS_DISABLED, VJS_BUSY, VJS_NO_DEVICE, VJS_FAILED };

/* The stick's USB state, for the setup check (hori_usb_state). */
enum { USBS_CONNECTED, USBS_NO_WINUSB, USBS_NOT_PLUGGED, USBS_CONNECTING };

extern int vj_ok, vj_state, vj_buttons, vj_cont_povs, vj_disc_povs;
extern LONG vj_sent, vj_failed;
extern char vj_error[200];

extern volatile LONG hori_quit;

/* Guards everything below, plus the wizard and log state in hori_gui.c. */
extern CRITICAL_SECTION hori_lock;

#define HORI_LOG_LINES 10
extern char hori_log_lines[HORI_LOG_LINES][160];
extern int hori_log_count;

extern struct hori_instance hori_inst;
extern int hori_connected;
extern int hori_ir_rate[4];     /* last second: full reads, short, timeouts, errors */

extern struct hori_cal hori_cal[AX_COUNT];
extern struct hori_btn_map hori_map[HB_COUNT];
extern const struct hori_btn_map hori_map_default[HB_COUNT];

/* Optional hooks, called with hori_lock held. */
extern void (*hori_log_hook)(const char *line);
extern void (*hori_sample_hook)(struct hori_instance *inst);

void hori_log(const char *fmt, ...);
const char *hori_vr_variant_name(int variant);
int hori_pressure(uint8_t raw);
int hori_cal_apply(const struct hori_cal *c, int v);
void hori_ini_path(char *out, size_t size);
void hori_cal_load(void);
void hori_cal_save(void);
void hori_map_save(void);
int hori_vjoy_axes_ok(void);
int hori_vjoy_buttons_needed(void);
void hori_vjoy_release(void);
void hori_vjoy_reinit(void);
int hori_usb_state(void);
uint64_t hori_button_word(const struct hori_instance *inst);
int hori_word_bit_have(const struct hori_instance *inst, int bit);
const char *hori_axis_label(int ax);

/* hori_gui.c */
void hori_gui_init(void);
void hori_gui(void);

/* hori_setup.c: the setup check (vJoy and the stick's WinUSB driver) */
extern HFONT gui_font, gui_bold, gui_small;
void setup_register_class(HINSTANCE hi);
void setup_open(HWND owner);
int setup_all_ok(void);

/* hori_about.c: credits and licenses */
void about_register_class(HINSTANCE hi);
void about_open(HWND owner);

#endif
