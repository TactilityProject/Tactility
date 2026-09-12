#include <sdkconfig.h>
#if CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbdevice.h>
#include <tactility/drivers/usb_cdc_device.h>
#include <tactility/drivers/usb_device_controller.h>
#include <tactility/log.h>

#include <tinyusb.h>
#include <tinyusb_cdc_acm.h>
#include <esp_log.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#define TAG "esp32_usb_cdc_device"
#define GET_CONFIG(device) ((const Esp32UsbDeviceChildConfig*)(device)->config)

// ---- CDC device state ----
// A single presence flag, flipped by start_device()/stop_device() - matches how the USB device
// controller checks "is this board's usbdevicecdc0 child enabled" (usb_cdc_device_is_present()),
// independent of whether the console is actually installed right now (build_contribution()/
// start_console()/stop_console() are called by the controller, not tied to this device's own
// start/stop, since CDC's console lifecycle follows whichever primary claim is active - HID or
// MIDI; MSC is deliberately excluded, see esp32_usb_device_controller.cpp's claim()).

struct UsbCdcDeviceCtx {
    bool present = false;
};

// ---- CDC console (log mirroring over the CDC ACM interface) ----

static bool cdc_console_installed = false;
static vprintf_like_t previous_vprintf = nullptr;

// Mirrors every log line to the CDC ACM interface in addition to whatever the log vprintf
// hook already does (UART0, ...) - deliberately additive rather than esp_tinyusb's own
// esp_tusb_init_console(), which freopen()s stdout/stderr and would replace the existing
// console instead of adding a second one alongside it.
static int cdc_mirroring_vprintf(const char* fmt, va_list args) {
    // args must be copied before the first vprintf-family call consumes it - passing an
    // already-consumed va_list to vsnprintf below is undefined behavior.
    va_list cdc_args;
    va_copy(cdc_args, args);

    int result = previous_vprintf ? previous_vprintf(fmt, args) : vprintf(fmt, args);

    if (tud_cdc_connected()) {
        char buf[256];
        int len = vsnprintf(buf, sizeof(buf), fmt, cdc_args);
        if (len > 0) {
            size_t to_write = static_cast<size_t>(len) < sizeof(buf) ? static_cast<size_t>(len) : sizeof(buf) - 1;
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, reinterpret_cast<const uint8_t*>(buf), to_write);
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        }
    }
    va_end(cdc_args);
    return result;
}

// ---- CDC descriptor contribution ----

static char cdc_interface_string[32] = "Tactility Console";
static uint8_t cdc_descriptor_bytes[TUD_CDC_DESC_LEN]; // one CDC instance per board

// ---- CDC device API ----

static bool cdc_device_is_present(struct Device* device) {
    auto* ctx = static_cast<UsbCdcDeviceCtx*>(device_get_driver_data(device));
    return ctx != nullptr && ctx->present;
}

static error_t cdc_device_build_contribution(struct Device* device, struct Device* controller,
                                              uint8_t interface_string_index,
                                              struct UsbInterfaceContribution* out_contribution,
                                              const char** out_interface_string) {
    (void)device;
    // TUD_CDC_DESCRIPTOR consumes 2 interfaces internally (control at itfnum, data at itfnum+1 -
    // it emits its own IAD covering both, see usbd.h) - same shape as MIDI's 2-interface layout.
    // Also needs 2 IN endpoints (notif + data-in) and 1 OUT (data-out), mirroring the old
    // HID-composited layout (HID_EP_NOTIF/HID_EP_CDC_IN/HID_EP_CDC_OUT), just dynamically
    // assigned now instead of fixed literals.
    struct UsbInterfaceAllocation alloc;
    error_t err = usb_device_controller_allocate_interfaces(controller, /*interface_count=*/2,
                                                              /*in_endpoint_count=*/2, /*out_endpoint_count=*/1, &alloc);
    if (err != ERROR_NONE) {
        return err;
    }

    const uint8_t ep_notif   = alloc.first_in_endpoint;
    const uint8_t ep_data_in = (uint8_t)(alloc.first_in_endpoint + 1);
    const uint8_t ep_data_out = alloc.first_out_endpoint;

    const uint8_t built[] = {
        TUD_CDC_DESCRIPTOR(alloc.first_interface_number, interface_string_index,
                            ep_notif, 8, ep_data_out, ep_data_in, 64),
    };
    memcpy(cdc_descriptor_bytes, built, sizeof(built));

    out_contribution->descriptor_bytes = cdc_descriptor_bytes;
    out_contribution->descriptor_bytes_len = sizeof(cdc_descriptor_bytes);
    out_contribution->interface_count = 2;
    out_contribution->in_endpoint_count = 2;
    out_contribution->out_endpoint_count = 1;
    *out_interface_string = cdc_interface_string;
    return ERROR_NONE;
}

static error_t cdc_device_start_console(struct Device* device) {
    (void)device;
    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = nullptr,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    if (tinyusb_cdcacm_init(&acm_cfg) != ESP_OK) {
        LOG_E(TAG, "tinyusb_cdcacm_init failed - CDC console unavailable");
        return ERROR_RESOURCE;
    }
    previous_vprintf = esp_log_set_vprintf(cdc_mirroring_vprintf);
    cdc_console_installed = true;
    return ERROR_NONE;
}

static error_t cdc_device_stop_console(struct Device* device) {
    (void)device;
    if (!cdc_console_installed) {
        return ERROR_NONE;
    }
    esp_log_set_vprintf(previous_vprintf ? previous_vprintf : vprintf);
    previous_vprintf = nullptr;
    tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    cdc_console_installed = false;
    return ERROR_NONE;
}

extern const UsbCdcDeviceApi esp32_usb_cdc_device_api = {
    .is_present         = cdc_device_is_present,
    .build_contribution = cdc_device_build_contribution,
    .start_console      = cdc_device_start_console,
    .stop_console       = cdc_device_stop_console,
};

// ---- Driver lifecycle ----
// Defined in each board's .dts as a child of usbdevice0 (usbdevicecdc0). Presence is boolean -
// this driver doesn't participate in usb_device_controller_claim()/release() itself; the
// controller discovers this device once via device_for_each_child() in its own start_device()
// and calls is_present()/build_contribution()/start_console()/stop_console() directly.

extern "C" {

static error_t start_device(struct Device* device) {
    (void)GET_CONFIG(device); // no configuration - placeholder only
    auto* ctx = new UsbCdcDeviceCtx();
    ctx->present = true;
    device_set_driver_data(device, ctx);
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbCdcDeviceCtx*>(device_get_driver_data(device));
    delete ctx;
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

Driver esp32_usb_cdc_device_driver = {
    .name         = "esp32_usb_cdc_device",
    .compatible   = (const char*[]) { "espressif,esp32-usbdevice-cdc", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = &esp32_usb_cdc_device_api,
    .device_type  = &USB_CDC_DEVICE_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED
