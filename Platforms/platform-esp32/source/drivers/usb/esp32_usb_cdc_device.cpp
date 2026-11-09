#include <sdkconfig.h>
#if CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbdevice.h>
#include <tactility/drivers/usb_cdc_device.h>
#include <tactility/drivers/usb_device_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <tinyusb.h>
#include <tinyusb_cdc_acm.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#if CONFIG_IDF_TARGET_ESP32S31
// Always-on LP_SYSTEM block: unlike most targets (RTC_CNTL_OPTION1_REG), the S31 keeps its
// "force ROM download boot" bit here.
#include <soc/lp_system_reg.h>
#endif

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

    // Mirror whenever the USB device is configured rather than only while DTR is asserted.
    // idf_monitor leaves DTR deasserted in its idle state, which would otherwise suppress all
    // output. The TX FIFO is overwritable while DTR is low, so nothing accumulates unread.
    if (tud_mounted()) {
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

// ---- ROM download-mode request over the CDC console ----
// The host sends this string on the console to reset the chip into the ROM's USB download
// mode, which avoids the BOOT button and a power cycle.
constexpr char DOWNLOAD_REQUEST[] = "TT:BOOTDL";

#if CONFIG_IDF_TARGET_ESP32S31
static esp_timer_handle_t download_mode_timer = nullptr;

static void clear_download_mode_request() {
    REG_WRITE(LP_SYSTEM_REG_SYS_CTRL_REG,
              REG_READ(LP_SYSTEM_REG_SYS_CTRL_REG) & ~LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
}

static void download_mode_timer_cb(void*) {
    LOG_I(TAG, "Rebooting into ROM download mode");
    REG_WRITE(LP_SYSTEM_REG_SYS_CTRL_REG,
              REG_READ(LP_SYSTEM_REG_SYS_CTRL_REG) | LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
#endif

static void request_download_mode() {
#if CONFIG_IDF_TARGET_ESP32S31
    if (download_mode_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = download_mode_timer_cb;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "download_mode";
        if (esp_timer_create(&args, &download_mode_timer) != ESP_OK) {
            LOG_E(TAG, "Failed to create the download-mode timer");
            return;
        }
    }
    // Deferred: the chip must not be restarted from inside the TinyUSB task/callback.
    esp_timer_start_once(download_mode_timer, 100 * 1000);
#else
    LOG_W(TAG, "ROM download mode reboot is not supported on this target");
#endif
}

// Rolling matcher for the request string: RX data is arbitrary and may be split across reads.
static char rx_accumulator[32] = {};
static size_t rx_accumulator_length = 0;

static void cdc_rx_callback(int itf, cdcacm_event_t* event) {
    if (event != nullptr && event->type != CDC_EVENT_RX) {
        return;
    }

    uint8_t buffer[32];
    size_t received = 0;
    if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buffer, sizeof(buffer), &received) != ESP_OK) {
        return;
    }

    for (size_t i = 0; i < received; ++i) {
        if (rx_accumulator_length >= sizeof(rx_accumulator) - 1) {
            memmove(rx_accumulator, rx_accumulator + 1, sizeof(rx_accumulator) - 2);
            rx_accumulator_length--;
        }
        rx_accumulator[rx_accumulator_length++] = static_cast<char>(buffer[i]);
        rx_accumulator[rx_accumulator_length] = '\0';
        if (strstr(rx_accumulator, DOWNLOAD_REQUEST) != nullptr) {
            rx_accumulator_length = 0;
            rx_accumulator[0] = '\0';
            request_download_mode();
            return;
        }
    }
}

// ---- CDC descriptor contribution ----

static char cdc_interface_string[32] = "Tactility Console";
static uint8_t cdc_descriptor_bytes[TUD_CDC_DESC_LEN]; // one CDC instance per board
#if (TUD_OPT_HIGH_SPEED)
static uint8_t cdc_hs_descriptor_bytes[TUD_CDC_DESC_LEN];
#endif

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
#if (TUD_OPT_HIGH_SPEED)
    // TinyUSB requires high-speed bulk endpoints to be exactly 512 bytes, so the full-speed
    // 64-byte endpoints above cannot be reused for an HS port (ESP32-P4 HS / ESP32-S31).
    const uint8_t built_hs[] = {
        TUD_CDC_DESCRIPTOR(alloc.first_interface_number, interface_string_index,
                            ep_notif, 8, ep_data_out, ep_data_in, 512),
    };
    memcpy(cdc_hs_descriptor_bytes, built_hs, sizeof(built_hs));
    out_contribution->hs_descriptor_bytes = cdc_hs_descriptor_bytes;
    out_contribution->hs_descriptor_bytes_len = sizeof(cdc_hs_descriptor_bytes);
#else
    out_contribution->hs_descriptor_bytes = nullptr;
    out_contribution->hs_descriptor_bytes_len = 0;
#endif
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
        .callback_rx = cdc_rx_callback,
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
#if CONFIG_IDF_TARGET_ESP32S31
    // The ROM consumes the force-download bit on entry. Clear it here as well so that a later
    // software reset cannot fall back into download mode if the ROM ever leaves it set.
    clear_download_mode_request();
#endif
    auto* ctx = new UsbCdcDeviceCtx();
    ctx->present = true;
    device_set_driver_data(device, ctx);

    // Boards that expose no primary USB class at boot (e.g. no SD card for MSC) can request the
    // console stand alone, which is also the only way to get a serial port on such boards.
    if (GET_CONFIG(device)->console_at_boot) {
        struct Device* controller = device_get_parent(device);
        if (controller != nullptr) {
            error_t error = usb_device_controller_start_cdc_console(controller);
            if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to start boot-time CDC console (%s)", error_to_string(error));
            }
        }
    }

    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    if (GET_CONFIG(device)->console_at_boot) {
        struct Device* controller = device_get_parent(device);
        if (controller != nullptr) {
            usb_device_controller_stop_cdc_console(controller);
        }
    }

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
    .probe = nullptr,
    .api          = &esp32_usb_cdc_device_api,
    .device_type  = &USB_CDC_DEVICE_TYPE,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED
