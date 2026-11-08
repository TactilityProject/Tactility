#include <sdkconfig.h>
#if CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_HID_COUNT

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbdevice.h>
#include <tactility/drivers/hid_report_descriptors.h>
#include <tactility/drivers/usb_device_controller.h>
#include <tactility/drivers/usb_hid_device.h>
#include <tactility/log.h>

#include <tinyusb.h>
#include <tusb.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#define TAG "esp32_usb_hid_device"
#define GET_CONFIG(device) ((const Esp32UsbDeviceChildConfig*)(device)->config)

// ---- HID Report Maps ----
// Defined once in Platforms/platform-esp32/source/drivers/hid_report_descriptors.cpp and shared
// with esp32_ble_hid.cpp - HID report descriptors are transport-independent (same USB HID Usage
// Tables spec applies to both USB and BLE HID), so the same bytes work verbatim over either
// transport.

// Report IDs, matching the shared hid_report_map_* tables.
static constexpr uint8_t REPORT_ID_KEYBOARD = 1;
static constexpr uint8_t REPORT_ID_CONSUMER = 2;
static constexpr uint8_t REPORT_ID_MOUSE_SOLO = 1;   // USB_HID_DEVICE_MODE_MOUSE
static constexpr uint8_t REPORT_ID_MOUSE_COMBO = 3;  // USB_HID_DEVICE_MODE_KEYBOARD_MOUSE
static constexpr uint8_t REPORT_ID_GAMEPAD = 1;

// Active mode/report-map state - the report descriptor TinyUSB serves via
// tud_hid_descriptor_report_cb() must match whichever mode start() was last called with, since
// (unlike BLE's mutable GATT) USB's descriptor is fixed for the lifetime of one claim()
// session; switching modes means stop() + start() with the new mode, re-enumerating.
static enum UsbHidDeviceMode active_mode = USB_HID_DEVICE_MODE_KEYBOARD;

static const uint8_t* active_report_map() {
    switch (active_mode) {
        case USB_HID_DEVICE_MODE_MOUSE:          return hid_report_map_mouse;
        case USB_HID_DEVICE_MODE_KEYBOARD_MOUSE:  return hid_report_map_keyboard_consumer_mouse;
        case USB_HID_DEVICE_MODE_GAMEPAD:        return hid_report_map_gamepad;
        default:                                 return hid_report_map_keyboard_consumer; // KEYBOARD
    }
}

static size_t active_report_map_len() {
    switch (active_mode) {
        case USB_HID_DEVICE_MODE_MOUSE:          return hid_report_map_mouse_len;
        case USB_HID_DEVICE_MODE_KEYBOARD_MOUSE:  return hid_report_map_keyboard_consumer_mouse_len;
        case USB_HID_DEVICE_MODE_GAMEPAD:        return hid_report_map_gamepad_len;
        default:                                 return hid_report_map_keyboard_consumer_len;
    }
}

// Per-mode default product name and idProduct offset. Distinct idProduct per mode matters more
// than it might seem: most hosts (Windows in particular) cache device metadata - including
// which icon/appearance to show - keyed by VID/PID/serial, and a HID descriptor swap alone
// isn't always enough to make the host re-read it on a fast re-enumeration. Giving each mode
// its own PID makes the host treat a mode switch as connecting a genuinely different device,
// which reliably clears the stale-appearance problem (was previously showing "Keyboard" for
// every mode almost all the time).
static const char* default_name_for_mode(enum UsbHidDeviceMode mode) {
    switch (mode) {
        case USB_HID_DEVICE_MODE_MOUSE:          return "Tactility Mouse";
        case USB_HID_DEVICE_MODE_KEYBOARD_MOUSE:  return "Tactility Keyboard+Mouse";
        case USB_HID_DEVICE_MODE_GAMEPAD:        return "Tactility Gamepad";
        default:                                 return "Tactility Keyboard"; // KEYBOARD
    }
}

static uint16_t product_id_for_mode(enum UsbHidDeviceMode mode) {
    switch (mode) {
        case USB_HID_DEVICE_MODE_MOUSE:          return 0x4006;
        case USB_HID_DEVICE_MODE_KEYBOARD_MOUSE:  return 0x4007;
        case USB_HID_DEVICE_MODE_GAMEPAD:        return 0x4008;
        default:                                 return 0x4004; // KEYBOARD (unchanged - existing PID)
    }
}

// Set via usb_hid_device_set_name() before start(); empty means "use the mode's default name".
// Mirrors bluetooth_set_device_name()'s pattern of being set once before starting the profile.
static char custom_name[32] = {};

// HID's own interface/endpoint numbers come from allocate_interfaces() at claim time
// (hid_device_start()) - CDC, if the board's usbdevicecdc0 child is enabled,
// is composited in by the USB device controller itself after HID's own contribution,
// so HID no longer needs to know or care whether CDC exists.

// The report descriptor length varies by mode (KEYBOARD_MOUSE's combined map is the largest),
// so the config descriptor's total length is computed at claim time in hid_device_start().

// Built fresh in start() (see build_hid_device_descriptor()) so idProduct/iProduct can vary per
// mode - see default_name_for_mode()/product_id_for_mode() above for why. 
// Mutable for the same reason, plus: the USB device controller patches bDeviceClass/SubClass/Protocol at
// claim() time depending on whether CDC is composited in.
static tusb_desc_device_t hid_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A, // Espressif VID
    .idProduct          = 0x4004, // overwritten per-mode in start()
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

static const char hid_langid_descriptor[] = { 0x09, 0x04 };
// index 2 (iProduct) is overwritten per-mode/per-custom-name in start(), see
// build_hid_device_descriptor().
static char hid_product_string[sizeof(custom_name)] = "Tactility Keyboard";
// index 4 is the HID interface string, read directly by some HID apps/testers (gamepad testers, joystick tools) instead of iProduct (index 2).
// Point it at the same buffer as iProduct so both stay in sync with whatever mode/custom name is active.
static const char* hid_string_descriptor[] = {
    hid_langid_descriptor, "Tactility", hid_product_string, "123456", hid_product_string,
};

// Built fresh in hid_device_start(), sized for the active mode's report map. TinyUSB only reads
// this once during tinyusb_driver_install() (inside claim()), so it's safe to mutate between
// claim sessions (i.e. stop() + start() with a different mode) as long as it's finalized before
// claim() is called.
static uint8_t hid_configuration_descriptor[TUD_HID_DESC_LEN];

// ---- TinyUSB HID callbacks (required by the TinyUSB HID class driver) ----

extern "C" {

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return active_report_map();
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
    // LED state (caps/num/scroll lock) - not currently surfaced to callers.
}

} // extern "C"

// ---- HID device API ----

// Fills in idProduct/iProduct for the given mode, honoring a set_name() override if one was
// set. Must run before every claim() - the name/PID need to match whatever mode is about to
// start, not whatever they were left as from a previous session.
static void build_hid_device_descriptor(enum UsbHidDeviceMode mode) {
    hid_device_descriptor.idProduct = product_id_for_mode(mode);
    const char* name = custom_name[0] ? custom_name : default_name_for_mode(mode);
    strncpy(hid_product_string, name, sizeof(hid_product_string) - 1);
    hid_product_string[sizeof(hid_product_string) - 1] = '\0';
}

static error_t hid_device_start(struct Device* device, enum UsbHidDeviceMode mode) {
    auto* controller = device_get_parent(device);

    error_t begin_result = usb_device_controller_begin_claim(controller);
    if (begin_result != ERROR_NONE) {
        return begin_result;
    }

    struct UsbInterfaceAllocation alloc;
    error_t alloc_result = usb_device_controller_allocate_interfaces(controller, /*interface_count=*/1,
                                                                       /*in_endpoint_count=*/1, /*out_endpoint_count=*/0, &alloc);
    if (alloc_result != ERROR_NONE) {
        return alloc_result;
    }

    // active_mode/hid_device_descriptor commit only once resources are reserved - reads of
    // active_mode (tud_hid_descriptor_report_cb(), the mode gates in send_*) must keep matching
    // whatever session is actually installed if begin_claim()/allocate_interfaces() above failed.
    active_mode = mode;
    build_hid_device_descriptor(mode);

    const uint8_t hid_bytes[] = {
        TUD_HID_DESCRIPTOR(alloc.first_interface_number, 4, HID_ITF_PROTOCOL_NONE,
                            active_report_map_len(), alloc.first_in_endpoint, 16, 10),
    };
    memcpy(hid_configuration_descriptor, hid_bytes, sizeof(hid_bytes));

    struct UsbDeviceClaimConfig claim_config = {};
    claim_config.device_descriptor = &hid_device_descriptor;
    claim_config.string_descriptor = hid_string_descriptor;
    claim_config.string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
    claim_config.primary.descriptor_bytes = hid_configuration_descriptor;
    claim_config.primary.descriptor_bytes_len = sizeof(hid_configuration_descriptor);
    claim_config.primary.interface_count = 1;
    claim_config.primary.in_endpoint_count = 1;
    claim_config.primary.out_endpoint_count = 0;

    return usb_device_controller_claim(controller, USB_DEVICE_CLASS_HID_KEYBOARD, &claim_config);
}

static error_t hid_device_stop(struct Device* device) {
    auto* controller = device_get_parent(device);
    return usb_device_controller_release(controller, USB_DEVICE_CLASS_HID_KEYBOARD);
}

static error_t hid_device_set_name(struct Device* device, const char* name) {
    (void)device;
    if (name == nullptr) {
        custom_name[0] = '\0'; // clear override, fall back to the mode's default name
        return ERROR_NONE;
    }
    strncpy(custom_name, name, sizeof(custom_name) - 1);
    custom_name[sizeof(custom_name) - 1] = '\0';
    return ERROR_NONE;
}

// tud_hid_report() fails outright (no queueing) if the endpoint is still busy sending a
// previous report - a single fire-and-forget call is unsafe whenever a caller sends two reports
// back to back (e.g. press immediately followed by release: the second call can lose the race and drop the release, leaving
// the host thinking a key/button/consumer-control is still held down (which then either does
// nothing further or, for keys the host auto-repeats, keeps repeating). Retry with a short
// backoff instead of failing immediately.
static error_t hid_send_report(uint8_t report_id, const uint8_t* report, size_t len) {
    if (!tud_mounted()) {
        LOG_W(TAG, "hid_send_report(id=%d): tud_mounted() false - real disconnect, not just a busy endpoint", report_id);
        return ERROR_INVALID_STATE;
    }
    for (int attempt = 0; attempt < 10; attempt++) {
        if (tud_hid_ready() && tud_hid_report(report_id, report, static_cast<uint16_t>(len))) {
            return ERROR_NONE;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    LOG_W(TAG, "hid_send_report(id=%d): gave up after 10 retries (~50ms) - endpoint stayed busy/not-ready the whole time", report_id);
    return ERROR_RESOURCE;
}

static error_t hid_device_send_keyboard(struct Device* device, const uint8_t* report, size_t len) {
    (void)device;
    if (active_mode != USB_HID_DEVICE_MODE_KEYBOARD && active_mode != USB_HID_DEVICE_MODE_KEYBOARD_MOUSE) {
        return ERROR_NOT_SUPPORTED;
    }
    uint8_t buf[8] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_send_report(REPORT_ID_KEYBOARD, buf, sizeof(buf));
}

static error_t hid_device_send_consumer(struct Device* device, const uint8_t* report, size_t len) {
    (void)device;
    if (active_mode != USB_HID_DEVICE_MODE_KEYBOARD && active_mode != USB_HID_DEVICE_MODE_KEYBOARD_MOUSE) {
        return ERROR_NOT_SUPPORTED;
    }
    uint8_t buf[2] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_send_report(REPORT_ID_CONSUMER, buf, sizeof(buf));
}

static error_t hid_device_send_mouse(struct Device* device, const uint8_t* report, size_t len) {
    (void)device;
    uint8_t report_id;
    if (active_mode == USB_HID_DEVICE_MODE_MOUSE) {
        report_id = REPORT_ID_MOUSE_SOLO;
    } else if (active_mode == USB_HID_DEVICE_MODE_KEYBOARD_MOUSE) {
        report_id = REPORT_ID_MOUSE_COMBO;
    } else {
        return ERROR_NOT_SUPPORTED;
    }
    uint8_t buf[4] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_send_report(report_id, buf, sizeof(buf));
}

static error_t hid_device_send_gamepad(struct Device* device, const uint8_t* report, size_t len) {
    (void)device;
    if (active_mode != USB_HID_DEVICE_MODE_GAMEPAD) {
        return ERROR_NOT_SUPPORTED;
    }
    // 8 bytes: X,Y,Rx,Ry,Z (1 each - Xbox-360-style, Z shared by triggers), hat/dpad (1),
    // buttons[2] (10 buttons + 6 padding bits). See hid_report_descriptors.cpp for the full
    // layout/rationale.
    uint8_t buf[8] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_send_report(REPORT_ID_GAMEPAD, buf, sizeof(buf));
}

static bool hid_device_is_connected(struct Device* device) {
    (void)device;
    // tud_hid_ready() deliberately NOT included here: it reflects whether the endpoint is idle
    // right now (false during every in-flight report send, which is normal and momentary, not
    // a disconnect), not whether a host is actually connected. Including it made this function
    // flap constantly under any traffic - explains the "Connected" <-> "waiting for host..."
    // flicker apps were seeing on every keypress/click even though nothing was actually wrong
    // (hid_send_report()'s own retry logic was succeeding the whole time; no warning ever
    // logged because there was nothing to warn about). tud_mounted() alone is the correct
    // "is a host connected" signal.
    return tud_mounted();
}

extern const UsbHidDeviceApi esp32_usb_hid_device_api = {
    .start         = hid_device_start,
    .stop          = hid_device_stop,
    .set_name      = hid_device_set_name,
    .send_keyboard = hid_device_send_keyboard,
    .send_consumer = hid_device_send_consumer,
    .send_mouse    = hid_device_send_mouse,
    .send_gamepad  = hid_device_send_gamepad,
    .is_connected  = hid_device_is_connected,
};

// ---- Driver lifecycle ----
// Defined in each board's .dts as a child of usbdevice0 (e.g. usbdevicehid0) - the devicetree
// compiler wires device_get_parent() to the controller automatically.

extern "C" {

static error_t start_device(struct Device* device) {
    (void)GET_CONFIG(device); // no configuration - placeholder only
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    // Safety cleanup: release the slot if it's still held (e.g. this device is stopped while
    // HID is still active).
    auto* controller = device_get_parent(device);
    if (controller != nullptr && usb_device_controller_get_active_class(controller) == USB_DEVICE_CLASS_HID_KEYBOARD) {
        usb_device_controller_release(controller, USB_DEVICE_CLASS_HID_KEYBOARD);
    }
    return ERROR_NONE;
}

Driver esp32_usb_hid_device_driver = {
    .name         = "esp32_usb_hid_device",
    .compatible   = (const char*[]) { "espressif,esp32-usbdevice-hid", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .probe = nullptr,
    .api          = &esp32_usb_hid_device_api,
    .device_type  = &USB_HID_DEVICE_TYPE,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_HID_COUNT
