#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

#include <libusb-1.0/libusb.h>

#include <linux/uinput.h>
#include <linux/joystick.h>

#define HORI_VENDOR_ID          0x06d3
#define HORI_PRODUCT_ID         0x0f10

#define HORI_POLL_VR0           0x00
#define HORI_POLL_VR1           0x01
#define HORI_RUMBLE_VR          0x0c

#define HORI_HAT_X      ABS_TILT_X // or ABS_RX
#define HORI_HAT_Y      ABS_TILT_Y // or ABS_RY

// New axes go after the existing ones so axis numbering in games doesn't shift.
#ifndef ABS_PROFILE
#define ABS_PROFILE     0x21
#endif
#define HORI_PRESSURE_A ABS_VOLUME
#define HORI_PRESSURE_B ABS_PROFILE
#define HORI_MODE_ABS   ABS_MISC
#define HORI_DIAL_1     ABS_THROTTLE
#define HORI_DIAL_2     ABS_WHEEL

#define HORI_FF_MAX     16
#define HORI_MAX_DIALS  4

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

/* How the M1/M2/M3 switch is exposed. */
enum hori_mode_style {
	HORI_MODE_OFF,          /* ignored */
	HORI_MODE_BUTTONS,      /* 3 extra buttons, the selected one is held */
	HORI_MODE_AXIS,         /* one axis: 0 = M1, 1 = M2, 2 = M3 */
	HORI_MODE_SHIFT,        /* every button gets a different code in M2 and M3 */
};

/* Logical digital buttons. */
enum {
	HB_TRIGGER, HB_LAUNCH, HB_FIRE_C, HB_A, HB_B, HB_HAT, HB_SW1, HB_D, HB_ST,
	HB_DPAD1_TOP, HB_DPAD1_RIGHT, HB_DPAD1_BOTTOM, HB_DPAD1_LEFT,
	HB_DPAD2_TOP, HB_DPAD2_RIGHT, HB_DPAD2_BOTTOM, HB_DPAD2_LEFT,
	HB_DPAD3_LEFT, HB_DPAD3_MIDDLE, HB_DPAD3_RIGHT,
	HB_COUNT
};

/* Button codes per layer. Layer 0 is the original mapping, so existing bindings keep working.
 * Layers 1 and 2 are only used with "-m shift" (M2 and M3). */
static const int hori_btn_codes[3][HB_COUNT] = {
	{
		BTN_TRIGGER, BTN_THUMB, BTN_TRIGGER_HAPPY1, BTN_A, BTN_B,
		BTN_TRIGGER_HAPPY3, BTN_Y, BTN_TRIGGER_HAPPY2, BTN_TRIGGER_HAPPY4,
		BTN_TRIGGER_HAPPY5, BTN_TRIGGER_HAPPY6, BTN_TRIGGER_HAPPY7, BTN_TRIGGER_HAPPY8,
		BTN_Z, BTN_TL, BTN_TR, BTN_MODE,
		BTN_X, BTN_C, BTN_THUMB2,
	},
	{
		BTN_TRIGGER_HAPPY9, BTN_TRIGGER_HAPPY10, BTN_TRIGGER_HAPPY11, BTN_TRIGGER_HAPPY12,
		BTN_TRIGGER_HAPPY13, BTN_TRIGGER_HAPPY14, BTN_TRIGGER_HAPPY15, BTN_TRIGGER_HAPPY16,
		BTN_TRIGGER_HAPPY17, BTN_TRIGGER_HAPPY18, BTN_TRIGGER_HAPPY19, BTN_TRIGGER_HAPPY20,
		BTN_TRIGGER_HAPPY21, BTN_TRIGGER_HAPPY22, BTN_TRIGGER_HAPPY23, BTN_TRIGGER_HAPPY24,
		BTN_TRIGGER_HAPPY25, BTN_TRIGGER_HAPPY26, BTN_TRIGGER_HAPPY27, BTN_TRIGGER_HAPPY28,
	},
	{
		BTN_TRIGGER_HAPPY29, BTN_TRIGGER_HAPPY30, BTN_TRIGGER_HAPPY31, BTN_TRIGGER_HAPPY32,
		BTN_TRIGGER_HAPPY33, BTN_TRIGGER_HAPPY34, BTN_TRIGGER_HAPPY35, BTN_TRIGGER_HAPPY36,
		BTN_TRIGGER_HAPPY37, BTN_TRIGGER_HAPPY38, BTN_TRIGGER_HAPPY39, BTN_TRIGGER_HAPPY40,
		BTN_TOP, BTN_TOP2, BTN_PINKIE, BTN_BASE,
		BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_BASE5,
	},
};

/* With "-m buttons", M1/M2/M3 are these (layers 1/2 are unused in that mode). */
static const int hori_mode_btn_codes[3] = { BTN_TRIGGER_HAPPY9, BTN_TRIGGER_HAPPY10, BTN_TRIGGER_HAPPY11 };

struct hori_dial {
	int	iface;
	uint8_t	ep;
	uint16_t	maxpkt;
	uint8_t	protocol;      /* HID boot protocol: 1 = keyboard, 2 = mouse */
};

struct hori_instance {
	struct hori_input_ir	state_ir;
	struct hori_input_vr_00	state_vr00;
	struct hori_input_vr_01	state_vr01;
	int have_ir, have_vr00, have_vr01;

	/* last raw data printed in debug mode */
	uint8_t	dbg_raw[12];

	/* what has been reported to uinput */
	uint8_t	btn[HB_COUNT];
	int	layer;
	int	mode;           /* 0 = M1, 1 = M2, 2 = M3, -1 = unknown */
	int	abs_last[ABS_CNT];

	/* rumble */
	struct ff_effect	ff_effects[HORI_FF_MAX];
	uint8_t	ff_used[HORI_FF_MAX];
	uint64_t	ff_start[HORI_FF_MAX];
	uint64_t	ff_end[HORI_FF_MAX];   /* 0 = not playing */
	uint32_t	ff_gain;
	int	rumble_sent;

	/* experimental dial capture */
	struct hori_dial	dials[HORI_MAX_DIALS];
	int	dial_count;
	int	dial_pos[2];

	int uinp_fd;

	struct libusb_device *usbdev;
	struct libusb_device_handle *usbdev_handle;

	struct hori_instance *prev, *next;
};

struct hori_instance *hori_handles = NULL;

static int opt_mode = HORI_MODE_BUTTONS;
static int opt_threshold = 20;
static int opt_rumble = 1;
static int opt_debug = 0;
static int opt_dials = 0;
static int opt_probe = 0;

static volatile sig_atomic_t hori_quit = 0;

static uint64_t hori_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void hori_uinput_setup_abs(struct hori_instance *inst, int code, int min, int max)
{
	struct uinput_abs_setup setup;
	memset( &setup, 0, sizeof(setup) );
	setup.code = code;
	setup.absinfo.minimum = min;
	setup.absinfo.maximum = max;
	setup.absinfo.fuzz = 0;
	setup.absinfo.flat = 0;
	setup.absinfo.resolution = 0;
	setup.absinfo.value = min;
	ioctl(inst->uinp_fd, UI_SET_ABSBIT, code);
	ioctl(inst->uinp_fd, UI_ABS_SETUP, &setup);
}

int hori_uinput_init(struct hori_instance *inst)
{
	int err, i, l;
	inst->uinp_fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
	if(inst->uinp_fd < 0) {
		fprintf(stderr, "hori_uinput_init: open(\"/dev/uinput\") failed: %s\n",
				strerror(errno));
		return -1;
	}

	err = ioctl(inst->uinp_fd, UI_SET_EVBIT, EV_SYN);
	if(err < 0) {
		fprintf(stderr, "hori_uinput_init: ioctl(%d, UI_SET_EVBIT, EV_SYN) failed: %s\n",
				inst->uinp_fd, strerror(errno));
		goto fail;
	}

	err = ioctl(inst->uinp_fd, UI_SET_EVBIT, EV_KEY);
	if(err < 0) {
		fprintf(stderr, "hori_uinput_init: ioctl(%d, UI_SET_EVBIT, EV_KEY) failed: %s\n",
				inst->uinp_fd, strerror(errno));
		goto fail;
	}

	int layers = (HORI_MODE_SHIFT == opt_mode) ? 3 : 1;
	for( l=0; l < layers; l++ )
		for( i=0; i < HB_COUNT; i++ )
			ioctl(inst->uinp_fd, UI_SET_KEYBIT, hori_btn_codes[l][i]);

	if( HORI_MODE_BUTTONS == opt_mode )
		for( i=0; i < 3; i++ )
			ioctl(inst->uinp_fd, UI_SET_KEYBIT, hori_mode_btn_codes[i]);

	ioctl(inst->uinp_fd, UI_SET_EVBIT, EV_ABS);
	hori_uinput_setup_abs(inst, ABS_X, 0, 255);
	hori_uinput_setup_abs(inst, ABS_Y, 0, 255);
	hori_uinput_setup_abs(inst, ABS_RUDDER, 0, 255);
	hori_uinput_setup_abs(inst, ABS_GAS, 0, 255);
	hori_uinput_setup_abs(inst, HORI_HAT_X, 0, 255);
	hori_uinput_setup_abs(inst, HORI_HAT_Y, 0, 255);
	hori_uinput_setup_abs(inst, HORI_PRESSURE_A, 0, 255);
	hori_uinput_setup_abs(inst, HORI_PRESSURE_B, 0, 255);

	if( HORI_MODE_AXIS == opt_mode )
		hori_uinput_setup_abs(inst, HORI_MODE_ABS, 0, 2);

	if( opt_dials ) {
		hori_uinput_setup_abs(inst, HORI_DIAL_1, 0, 255);
		hori_uinput_setup_abs(inst, HORI_DIAL_2, 0, 255);
	}

	if( opt_rumble ) {
		ioctl(inst->uinp_fd, UI_SET_EVBIT, EV_FF);
		ioctl(inst->uinp_fd, UI_SET_FFBIT, FF_RUMBLE);
		ioctl(inst->uinp_fd, UI_SET_FFBIT, FF_GAIN);
	}

	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Mitsubishi HORI/Namco Flightstick 2");
	setup.id.bustype = BUS_USB;
	setup.id.vendor = HORI_VENDOR_ID;
	setup.id.product = HORI_PRODUCT_ID;
	setup.id.version = 1;
	setup.ff_effects_max = opt_rumble ? HORI_FF_MAX : 0;

	err = ioctl(inst->uinp_fd, UI_DEV_SETUP, &setup);
	if(err < 0) {
		fprintf(stderr, "hori_uinput_init: ioctl(%d, UI_DEV_SETUP) failed: %s\n",
				inst->uinp_fd, strerror(errno));
		goto fail;
	}

	err = ioctl(inst->uinp_fd, UI_DEV_CREATE);
	if(err < 0) {
		fprintf(stderr, "hori_uinput_init: ioctl(%d, UI_DEV_CREATE) failed: %s\n",
				inst->uinp_fd, strerror(errno));
		goto fail;
	}

	for( i=0; i < ABS_CNT; i++ )
		inst->abs_last[i] = -1;
	inst->mode = -1;
	inst->ff_gain = 0xffff;
	inst->dial_pos[0] = inst->dial_pos[1] = 128;

	return 0;

fail:
	close(inst->uinp_fd);
	inst->uinp_fd = -1;
	return -1;
}

void hori_uinput_shutdown(struct hori_instance *inst)
{
	if( inst->uinp_fd < 0 )
		return;

	if(ioctl(inst->uinp_fd, UI_DEV_DESTROY) < 0) {
		fprintf(stderr, "hori_uinput_shutdown: ioctl(%d, UI_DEV_DESTROY) failed.\n", inst->uinp_fd);
	}
	close(inst->uinp_fd);
	inst->uinp_fd = -1;
}

static inline int hori_uinput_emit(struct hori_instance *inst, int type, int code, int val)
{
	struct input_event ie;

	memset(&ie, 0, sizeof(ie));
	ie.type = type;
	ie.code = code;
	ie.value = val;

	int ret = write(inst->uinp_fd, &ie, sizeof(ie));
	if( ret < 0 ) {
		fprintf(stderr, "hori_uinput_emit: write(%d, data, %zu) failed: %s\n",
				inst->uinp_fd, sizeof(ie), strerror(errno));
		return -1;
	}
	return 0;
}

static inline int hori_set_abs(struct hori_instance *inst, int code, int val, int *doreport)
{
	if( inst->abs_last[code] == val )
		return 0;

	inst->abs_last[code] = val;
	*doreport = 1;
	return hori_uinput_emit(inst, EV_ABS, code, val);
}

struct hori_instance *hori_instance_find(struct libusb_device *usbdev)
{
	struct hori_instance *i;
	for( i=hori_handles; i; i = i->next )
	{
		if( i->usbdev == usbdev )
			return i;
	}

	return NULL;
}

void hori_instance_insert(struct hori_instance *inst)
{
	if( hori_handles )
		hori_handles->prev = inst;

	inst->next = hori_handles;
	hori_handles = inst;
}

void hori_instance_remove(struct hori_instance *inst)
{
	if( inst->prev )
		inst->prev->next = inst->next;
	else
		hori_handles = inst->next;

	if( inst->next )
		inst->next->prev = inst->prev;

	inst->prev = inst->next = NULL;
}

static void hori_usb_dump_config(struct hori_instance *inst)
{
	struct libusb_config_descriptor *cfg;
	if( LIBUSB_SUCCESS != libusb_get_active_config_descriptor(inst->usbdev, &cfg) ) {
		fprintf(stderr, "hori_usb_dump_config: Could not read config descriptor.\n");
		return;
	}

	fprintf(stderr, "hori: %d interface(s)\n", cfg->bNumInterfaces);
	for( int i=0; i < cfg->bNumInterfaces; i++ )
	{
		for( int a=0; a < cfg->interface[i].num_altsetting; a++ )
		{
			const struct libusb_interface_descriptor *id = &cfg->interface[i].altsetting[a];
			fprintf(stderr, "hori:   interface %d alt %d: class %02x subclass %02x protocol %02x, %d endpoint(s)\n",
					id->bInterfaceNumber, id->bAlternateSetting,
					id->bInterfaceClass, id->bInterfaceSubClass, id->bInterfaceProtocol,
					id->bNumEndpoints);
			for( int e=0; e < id->bNumEndpoints; e++ )
			{
				const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
				fprintf(stderr, "hori:     endpoint %02x attr %02x maxpkt %d interval %d\n",
						ep->bEndpointAddress, ep->bmAttributes,
						ep->wMaxPacketSize, ep->bInterval);
			}
		}
	}

	libusb_free_config_descriptor(cfg);
}

/* Experimental: the dials may come in on separate HID mouse/keyboard interfaces.
 * Grab any extra interface that has an interrupt IN endpoint and read it ourselves. */
static void hori_usb_init_dials(struct hori_instance *inst)
{
	struct libusb_config_descriptor *cfg;
	if( LIBUSB_SUCCESS != libusb_get_active_config_descriptor(inst->usbdev, &cfg) )
		return;

	for( int i=0; i < cfg->bNumInterfaces && inst->dial_count < HORI_MAX_DIALS; i++ )
	{
		if( cfg->interface[i].num_altsetting < 1 )
			continue;

		const struct libusb_interface_descriptor *id = &cfg->interface[i].altsetting[0];
		if( 0 == id->bInterfaceNumber )
			continue;

		for( int e=0; e < id->bNumEndpoints; e++ )
		{
			const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
			if( (ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_INTERRUPT )
				continue;
			if( !(ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) )
				continue;

			int err = libusb_claim_interface(inst->usbdev_handle, id->bInterfaceNumber);
			if( LIBUSB_SUCCESS != err ) {
				fprintf(stderr, "hori_usb_init_dials: Cannot claim interface %d: %s\n",
						id->bInterfaceNumber, libusb_strerror(err));
				break;
			}

			// HID boot subclass: ask for the fixed boot report layout.
			if( LIBUSB_CLASS_HID == id->bInterfaceClass && 1 == id->bInterfaceSubClass )
				libusb_control_transfer(inst->usbdev_handle,
						LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
						0x0b /* SET_PROTOCOL */, 0 /* boot */, id->bInterfaceNumber, NULL, 0, 200);

			struct hori_dial *d = &inst->dials[inst->dial_count++];
			d->iface = id->bInterfaceNumber;
			d->ep = ep->bEndpointAddress;
			d->maxpkt = ep->wMaxPacketSize > 64 ? 64 : ep->wMaxPacketSize;
			d->protocol = id->bInterfaceProtocol;
			fprintf(stderr, "hori_usb_init_dials: Reading interface %d endpoint %02x (protocol %d)\n",
					d->iface, d->ep, d->protocol);
			break;
		}
	}

	if( 0 == inst->dial_count )
		fprintf(stderr, "hori_usb_init_dials: No extra interrupt interfaces found; the dials aren't reported over USB on this unit.\n");

	libusb_free_config_descriptor(cfg);
}

/* Read-only probe of other vendor IN requests, to look for unknown controls. */
static void hori_usb_probe(struct hori_instance *inst)
{
	unsigned char buf[16];
	for( int req=0; req < 0x20; req++ )
	{
		memset(buf, 0, sizeof(buf));
		int ret = libusb_control_transfer(inst->usbdev_handle,
				LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT,
				req, 0, 1, buf, sizeof(buf), 200);
		if( ret < 0 ) {
			fprintf(stderr, "hori_usb_probe: request %02x: %s\n", req, libusb_strerror(ret));
			continue;
		}
		fprintf(stderr, "hori_usb_probe: request %02x: %d bytes:", req, ret);
		for( int i=0; i < ret; i++ )
			fprintf(stderr, " %02x", buf[i]);
		fprintf(stderr, "\n");
	}
}

int hori_usb_init(struct hori_instance *inst)
{
	int err = libusb_open(inst->usbdev, &inst->usbdev_handle);
	if(LIBUSB_SUCCESS != err) {
		fprintf(stderr, "hori_usb_init: Device could not be opened. Check that it is connected and that you have appropriate permissions.\n");
		inst->usbdev_handle = NULL;
		return err;
	}

	libusb_set_auto_detach_kernel_driver(inst->usbdev_handle, 1);

	err = libusb_claim_interface(inst->usbdev_handle, 0);
	if(LIBUSB_SUCCESS != err) {
		fprintf(stderr, "hori_usb_init: Cannot claim interface: %s\n", libusb_strerror(err));
		return err;
	}

	err = libusb_set_interface_alt_setting(inst->usbdev_handle, 0, 0);
	if(LIBUSB_SUCCESS != err) {
		fprintf(stderr, "hori_usb_init: Cannot set alternate setting: %s\n", libusb_strerror(err));
		return err;
	}

	if( opt_debug )
		hori_usb_dump_config(inst);

	if( opt_dials )
		hori_usb_init_dials(inst);

	if( opt_probe )
		hori_usb_probe(inst);

	return LIBUSB_SUCCESS;
}

struct hori_instance *hori_instance_alloc()
{
	struct hori_instance *inst = (struct hori_instance *)malloc(sizeof(struct hori_instance));

	memset(inst, 0, sizeof(struct hori_instance));
	inst->uinp_fd = -1;

	hori_instance_insert(inst);

	return inst;
}

void hori_instance_free(struct hori_instance *inst)
{
	hori_instance_remove(inst);
	free(inst);
}

static void hori_rumble_send(struct hori_instance *inst, uint8_t strength)
{
	int err = libusb_control_transfer(inst->usbdev_handle,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT,
			HORI_RUMBLE_VR, 0, 1, &strength, sizeof(strength), 200);
	if( err < 0 && opt_debug )
		fprintf(stderr, "hori_rumble_send: libusb_control_transfer failed: %d: %s\n", err, libusb_strerror(err));

	inst->rumble_sent = strength;
}

void hori_instance_close(struct hori_instance *inst, int unplugged)
{
	struct libusb_device_handle *dev_handle = inst->usbdev_handle;

	if( dev_handle && !unplugged && inst->rumble_sent )
		hori_rumble_send(inst, 0);

	if( dev_handle ) {
		for( int i=0; i < inst->dial_count; i++ )
			libusb_release_interface(dev_handle, inst->dials[i].iface);
		libusb_release_interface(dev_handle, 0);
	}

	hori_uinput_shutdown(inst);
	hori_instance_free(inst);
	if( unplugged )
		printf("hori_instance_close: Device disconnected\n");

	if( dev_handle )
		libusb_close(dev_handle);
}

static struct hori_instance *hori_instance_open(struct libusb_device *dev, const char *caller)
{
	if( hori_instance_find(dev) )
		return NULL;

	struct hori_instance *inst = hori_instance_alloc();
	inst->usbdev = dev;
	if( 0 != hori_uinput_init(inst) )
	{
		fprintf(stderr, "%s: Could not create a uinput instance for this device.\n", caller);
		hori_instance_free(inst);
		return NULL;
	}

	if( LIBUSB_SUCCESS != hori_usb_init(inst) )
	{
		fprintf(stderr, "%s: Could not open USB device\n", caller);
		hori_instance_close(inst, 0);
		return NULL;
	}

	return inst;
}

int hori_usb_hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev,
                     libusb_hotplug_event event, void *user_data) {
	if(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event)
	{
		if( hori_instance_open(dev, "hori_usb_hotplug_callback") )
			printf("hori_usb_hotplug_callback: Device connected\n");
	} else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event) {
		// This will usually not happen, because normally we
		// notice the device is DCed during polling.
		struct hori_instance *inst = hori_instance_find(dev);
		if( inst )
			hori_instance_close(inst, 1);
	} else {
		fprintf(stderr, "hori_usb_hotplug_callback: Unhandled event %d\n", event);
	}

	return 0;
}

/* A/B raw values idle at ~0xff and drop as they're pressed.
 * Scale to 0 (released) - 255 (fully pressed), same as hfsd. */
static inline int hori_pressure(uint8_t raw)
{
	int v = ((0xff - raw) - 0x26) * 0xff / 0xc2;
	if( v < 0 ) v = 0;
	if( v > 0xff ) v = 0xff;
	return v;
}

/* Digital press for an analog button, with a bit of hysteresis so it doesn't chatter. */
static inline int hori_pressure_btn(int was_pressed, int pressure)
{
	if( was_pressed )
		return pressure + 8 > opt_threshold;
	return pressure > opt_threshold;
}

int hori_poll_ir(struct hori_instance *inst)
{
	int transferred;
	struct hori_input_ir ir;

	int err = libusb_interrupt_transfer(inst->usbdev_handle, 0x81, (unsigned char *)&ir, sizeof(ir), &transferred, 20);

	// enh.
	if( LIBUSB_ERROR_TIMEOUT == err )
		return 0;

	if( 0 != err) {
		fprintf(stderr, "hori_poll_ir: libusb_interrupt_transfer failed: %d: %s\n", err, libusb_strerror(err));
		return -1;
	}

	if( transferred != sizeof(ir) ) {
		if( opt_debug )
			fprintf(stderr, "hori_poll_ir: Expected %zu bytes, received %d bytes.\n",
					sizeof(ir), transferred);
		return 0;
	}

	inst->state_ir = ir;
	inst->have_ir = 1;
	return 0;
}

static int hori_poll_vr(struct hori_instance *inst, uint8_t request, void *out, size_t size, int *have)
{
	uint8_t vr[2];

	int err = libusb_control_transfer(inst->usbdev_handle,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT,
			request, 0, 1, vr, size, 200);

	// Handle stall. (Do nothing, we're in a control xfer.)
	if( LIBUSB_ERROR_PIPE == err || LIBUSB_ERROR_TIMEOUT == err )
		return 0;

	if( 0 > err) {
		fprintf(stderr, "hori_poll_vr: request %02x: libusb_control_transfer failed: %d: %s\n", request, err, libusb_strerror(err));
		return -1;
	}

	if( (size_t)err != size ) {
		if( opt_debug )
			fprintf(stderr, "hori_poll_vr: request %02x: Expected %zu bytes, received %d bytes.\n",
					request, size, err);
		return 0;
	}

	memcpy(out, vr, size);
	*have = 1;
	return 0;
}

static int hori_poll_dials(struct hori_instance *inst, int *doreport)
{
	unsigned char buf[64];
	int transferred;

	for( int i=0; i < inst->dial_count; i++ )
	{
		struct hori_dial *d = &inst->dials[i];
		int err = libusb_interrupt_transfer(inst->usbdev_handle, d->ep, buf, d->maxpkt, &transferred, 1);
		if( LIBUSB_ERROR_TIMEOUT == err || 0 == transferred )
			continue;
		if( 0 != err ) {
			fprintf(stderr, "hori_poll_dials: interface %d: %s\n", d->iface, libusb_strerror(err));
			return -1;
		}

		if( opt_debug ) {
			fprintf(stderr, "hori: iface%d:", d->iface);
			for( int b=0; b < transferred; b++ )
				fprintf(stderr, " %02x", buf[b]);
			fprintf(stderr, "\n");
		}

		// Boot mouse report: buttons, dx, dy. Turn the relative motion into two absolute dials.
		if( 2 == d->protocol && transferred >= 3 ) {
			for( int axis=0; axis < 2; axis++ ) {
				int v = inst->dial_pos[axis] + (int8_t)buf[1 + axis];
				inst->dial_pos[axis] = v < 0 ? 0 : (v > 255 ? 255 : v);
			}
			if( 0 > hori_set_abs(inst, HORI_DIAL_1, inst->dial_pos[0], doreport) ) return -1;
			if( 0 > hori_set_abs(inst, HORI_DIAL_2, inst->dial_pos[1], doreport) ) return -1;
		}
	}

	return 0;
}

static void hori_debug_dump(struct hori_instance *inst)
{
	uint8_t raw[12];
	memcpy(raw, &inst->state_ir, 8);
	memcpy(raw + 8, &inst->state_vr00, 2);
	memcpy(raw + 10, &inst->state_vr01, 2);
	if( 0 == memcmp(raw, inst->dbg_raw, sizeof(raw)) )
		return;
	memcpy(inst->dbg_raw, raw, sizeof(raw));

	fprintf(stderr, "hori: ir:");
	for( int i=0; i < 8; i++ )
		fprintf(stderr, " %02x", raw[i]);
	fprintf(stderr, "  vr00: %02x %02x  vr01: %02x %02x\n", raw[8], raw[9], raw[10], raw[11]);
}

/* Turn the latest raw state into uinput events. */
static int hori_report(struct hori_instance *inst)
{
	int doreport = 0;
	int i;
	uint8_t btn[HB_COUNT];
	memcpy(btn, inst->btn, sizeof(btn));

	if( inst->have_ir ) {
		struct hori_input_ir *ir = &inst->state_ir;
		if( 0 > hori_set_abs(inst, ABS_X, ir->pos_x, &doreport) ) return -1;
		if( 0 > hori_set_abs(inst, ABS_Y, ir->pos_y, &doreport) ) return -1;
		if( 0 > hori_set_abs(inst, ABS_RUDDER, ir->rudder, &doreport) ) return -1;
		if( 0 > hori_set_abs(inst, ABS_GAS, ir->throttle, &doreport) ) return -1;
		if( 0 > hori_set_abs(inst, HORI_HAT_X, ir->hat_x, &doreport) ) return -1;
		if( 0 > hori_set_abs(inst, HORI_HAT_Y, ir->hat_y, &doreport) ) return -1;

		int pa = hori_pressure(ir->btn_a);
		int pb = hori_pressure(ir->btn_b);
		if( 0 > hori_set_abs(inst, HORI_PRESSURE_A, pa, &doreport) ) return -1;
		if( 0 > hori_set_abs(inst, HORI_PRESSURE_B, pb, &doreport) ) return -1;
		btn[HB_A] = hori_pressure_btn(inst->btn[HB_A], pa);
		btn[HB_B] = hori_pressure_btn(inst->btn[HB_B], pb);
	}

	// Digital bits are active-low.
	if( inst->have_vr00 ) {
		struct hori_input_vr_00 *vr = &inst->state_vr00;
		btn[HB_TRIGGER]      = !vr->trigger;
		btn[HB_LAUNCH]       = !vr->launch;
		btn[HB_FIRE_C]       = !vr->fire_c;
		btn[HB_HAT]          = !vr->hat;
		btn[HB_D]            = !vr->button_d;
		btn[HB_ST]           = !vr->button_st;
		btn[HB_DPAD1_TOP]    = !vr->dpad1_top;
		btn[HB_DPAD1_RIGHT]  = !vr->dpad1_right;
		btn[HB_DPAD1_BOTTOM] = !vr->dpad1_bottom;
		btn[HB_DPAD1_LEFT]   = !vr->dpad1_left;
	}

	if( inst->have_vr01 ) {
		struct hori_input_vr_01 *vr = &inst->state_vr01;
		btn[HB_SW1]          = !vr->button_sw1;
		btn[HB_DPAD2_TOP]    = !vr->dpad2_top;
		btn[HB_DPAD2_RIGHT]  = !vr->dpad2_right;
		btn[HB_DPAD2_BOTTOM] = !vr->dpad2_bottom;
		btn[HB_DPAD2_LEFT]   = !vr->dpad2_left;
		btn[HB_DPAD3_LEFT]   = !vr->dpad3_left;
		btn[HB_DPAD3_MIDDLE] = !vr->dpad3_middle;
		btn[HB_DPAD3_RIGHT]  = !vr->dpad3_right;

		// 2 = M1, 1 = M2, 3 = M3. 0 shows up briefly while the switch is
		// moving; ignore it rather than reporting a bogus mode.
		static const int mode_map[4] = { -1, 1, 0, 2 };
		int mode = mode_map[vr->mode_select];
		if( mode >= 0 && mode != inst->mode && HORI_MODE_OFF != opt_mode ) {
			if( HORI_MODE_BUTTONS == opt_mode ) {
				if( inst->mode >= 0 && 0 > hori_uinput_emit(inst, EV_KEY, hori_mode_btn_codes[inst->mode], 0) )
					return -1;
				if( 0 > hori_uinput_emit(inst, EV_KEY, hori_mode_btn_codes[mode], 1) )
					return -1;
				doreport = 1;
			} else if( HORI_MODE_AXIS == opt_mode ) {
				if( 0 > hori_set_abs(inst, HORI_MODE_ABS, mode, &doreport) )
					return -1;
			}
			if( opt_debug )
				fprintf(stderr, "hori: mode M%d\n", mode + 1);
			inst->mode = mode;
		}
	}

	int layer = (HORI_MODE_SHIFT == opt_mode && inst->mode >= 0) ? inst->mode : 0;

	// On a layer change, let go of everything held on the old layer
	// so nothing gets stuck down.
	if( layer != inst->layer ) {
		for( i=0; i < HB_COUNT; i++ ) {
			if( !inst->btn[i] )
				continue;
			if( 0 > hori_uinput_emit(inst, EV_KEY, hori_btn_codes[inst->layer][i], 0) )
				return -1;
			inst->btn[i] = 0;
			doreport = 1;
		}
		inst->layer = layer;
	}

	for( i=0; i < HB_COUNT; i++ ) {
		if( btn[i] == inst->btn[i] )
			continue;
		if( 0 > hori_uinput_emit(inst, EV_KEY, hori_btn_codes[layer][i], btn[i]) )
			return -1;
		inst->btn[i] = btn[i];
		doreport = 1;
	}

	if( opt_dials && 0 > hori_poll_dials(inst, &doreport) )
		return -1;

	if( 1 == doreport )
		return hori_uinput_emit(inst, EV_SYN, SYN_REPORT, 0);

	return 0;
}

/* Service force feedback uploads/erases/playback from uinput, and drive the motor. */
static void hori_ff_process(struct hori_instance *inst)
{
	struct input_event ev;
	uint64_t now = hori_now_ms();

	while( read(inst->uinp_fd, &ev, sizeof(ev)) == sizeof(ev) )
	{
		if( EV_UINPUT == ev.type && UI_FF_UPLOAD == ev.code ) {
			struct uinput_ff_upload up;
			memset(&up, 0, sizeof(up));
			up.request_id = ev.value;
			if( ioctl(inst->uinp_fd, UI_BEGIN_FF_UPLOAD, &up) < 0 )
				continue;
			if( up.effect.id >= 0 && up.effect.id < HORI_FF_MAX && FF_RUMBLE == up.effect.type ) {
				inst->ff_effects[up.effect.id] = up.effect;
				inst->ff_used[up.effect.id] = 1;
				up.retval = 0;
			} else
				up.retval = -EINVAL;
			ioctl(inst->uinp_fd, UI_END_FF_UPLOAD, &up);
		} else if( EV_UINPUT == ev.type && UI_FF_ERASE == ev.code ) {
			struct uinput_ff_erase er;
			memset(&er, 0, sizeof(er));
			er.request_id = ev.value;
			if( ioctl(inst->uinp_fd, UI_BEGIN_FF_ERASE, &er) < 0 )
				continue;
			if( er.effect_id < HORI_FF_MAX ) {
				inst->ff_used[er.effect_id] = 0;
				inst->ff_end[er.effect_id] = 0;
			}
			er.retval = 0;
			ioctl(inst->uinp_fd, UI_END_FF_ERASE, &er);
		} else if( EV_FF == ev.type ) {
			if( FF_GAIN == ev.code ) {
				inst->ff_gain = ev.value & 0xffff;
			} else if( ev.code < HORI_FF_MAX && inst->ff_used[ev.code] ) {
				struct ff_effect *e = &inst->ff_effects[ev.code];
				if( ev.value > 0 ) {
					inst->ff_start[ev.code] = now + e->replay.delay;
					inst->ff_end[ev.code] = e->replay.length
						? inst->ff_start[ev.code] + (uint64_t)e->replay.length * ev.value
						: UINT64_MAX;
				} else
					inst->ff_end[ev.code] = 0;
			}
		}
	}

	uint32_t strength = 0;
	for( int i=0; i < HORI_FF_MAX; i++ ) {
		if( !inst->ff_end[i] )
			continue;
		if( now >= inst->ff_end[i] ) {
			inst->ff_end[i] = 0;
			continue;
		}
		if( now < inst->ff_start[i] )
			continue;

		// One motor: use whichever of the two magnitudes is stronger.
		uint32_t m = inst->ff_effects[i].u.rumble.strong_magnitude;
		if( inst->ff_effects[i].u.rumble.weak_magnitude > m )
			m = inst->ff_effects[i].u.rumble.weak_magnitude;
		if( m > strength )
			strength = m;
	}

	uint8_t out = (uint8_t)((strength * inst->ff_gain / 0xffff) >> 8);
	if( out != inst->rumble_sent )
		hori_rumble_send(inst, out);
}

int hori_poll()
{
	int err = 0;
	struct hori_instance *i, *n;
	for( i=hori_handles; i; i=n)
	{
		n = i->next;
		err = hori_poll_ir(i);

		if( 0 == err )
			err = hori_poll_vr(i, HORI_POLL_VR0, &i->state_vr00, sizeof(i->state_vr00), &i->have_vr00);

		if( 0 == err )
			err = hori_poll_vr(i, HORI_POLL_VR1, &i->state_vr01, sizeof(i->state_vr01), &i->have_vr01);

		if( 0 == err && opt_debug )
			hori_debug_dump(i);

		if( 0 == err )
			err = hori_report(i);

		if( 0 == err && opt_rumble )
			hori_ff_process(i);

		if( 0 != err )
			hori_instance_close(i, 1);
	}

	return err;
}

int hori_init_existing()
{
	int found = 0;
	libusb_device **devlist = NULL;
	ssize_t count = libusb_get_device_list(NULL, &devlist);
	for(ssize_t i = 0; i < count; i++)
	{
		struct libusb_device_descriptor desc;
		libusb_device *device = devlist[i];

		int ret = libusb_get_device_descriptor(device, &desc);
		if( LIBUSB_SUCCESS != ret )
			continue;

		if( HORI_VENDOR_ID != desc.idVendor )
			continue;

		if( HORI_PRODUCT_ID != desc.idProduct )
			continue;

		if( hori_instance_open(device, "hori_init_existing") ) {
			fprintf(stderr, "hori_init_existing: Device registered.\n");
			found++;
		}
	}
	libusb_free_device_list(devlist, 1);
	return found;
}

static void hori_signal(int sig)
{
	hori_quit = 1;
}

static void hori_usage(const char *argv0)
{
	printf("Usage: %s [options]\n"
		"  -m MODE   How to expose the M1/M2/M3 switch (default: buttons)\n"
		"              off      ignore it\n"
		"              buttons  3 buttons, the selected one is held down\n"
		"              axis     one axis: 0 = M1, 1 = M2, 2 = M3\n"
		"              shift    every button sends a different code in M1, M2 and M3\n"
		"  -t N      A/B press threshold, 0-254 on the 0-255 pressure scale (default: 20)\n"
		"  -R        Disable rumble\n"
		"  -k        Experimental: read the dials from any extra mouse/keyboard interface\n"
		"  -d        Debug: print USB descriptors and raw reports as they change\n"
		"  -P        Debug: probe vendor IN requests 0x00-0x1f once at connect\n"
		"  -h        This help\n", argv0);
}

int main(int argc, char **argv)
{
	int ret;
	int c;
	libusb_hotplug_callback_handle callback_handle;

	while( -1 != (c = getopt(argc, argv, "m:t:RkdPh")) )
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
			if( opt_threshold < 0 || opt_threshold > 254 ) { hori_usage(argv[0]); return EXIT_FAILURE; }
			break;
		case 'R': opt_rumble = 0; break;
		case 'k': opt_dials = 1; break;
		case 'd': opt_debug = 1; break;
		case 'P': opt_probe = 1; break;
		case 'h': hori_usage(argv[0]); return EXIT_SUCCESS;
		default: hori_usage(argv[0]); return EXIT_FAILURE;
		}
	}

	signal(SIGINT, hori_signal);
	signal(SIGTERM, hori_signal);

	libusb_init(NULL);

	//libusb_init_context(NULL, NULL, 0);

	ret = libusb_hotplug_register_callback(NULL,
			LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
			0,
			HORI_VENDOR_ID,
			HORI_PRODUCT_ID,
			LIBUSB_HOTPLUG_MATCH_ANY,
			hori_usb_hotplug_callback,
			NULL,
			&callback_handle);

	if (LIBUSB_SUCCESS != ret) {
		printf("Error creating a hotplug callback\n");
		libusb_exit(NULL);
		return EXIT_FAILURE;
	}

	// Try to pick up any devices currently connected.
	hori_init_existing();

	printf("Mitsubishi HORI/Namco Flightstick 2 driver loaded.\n");

	struct timeval tv;
	uint32_t chillout = 0;
	while (!hori_quit) {
		hori_poll();

		// effectively limit polling to 5ms intervals
		// to reduce cpu usage:
		tv.tv_sec = 0;
		tv.tv_usec = 5000;

		// only check hotplug events every 5 seconds
		// to reduce cpu usage:
		if( chillout++ % 1000 == 0 ) {
			// if no devices, block for a while (Ctrl+C still gets us out)
			if( NULL == hori_handles )
				tv.tv_sec = 5;

			libusb_handle_events_timeout_completed(NULL, &tv, NULL);
		} else
			select(0, NULL, NULL, NULL, &tv);
	}

	while( hori_handles )
		hori_instance_close(hori_handles, 0);

	libusb_hotplug_deregister_callback(NULL, callback_handle);
	libusb_exit(NULL);

	return 0;
}
