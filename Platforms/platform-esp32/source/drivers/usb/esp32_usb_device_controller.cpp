#include <sdkconfig.h>
#if CONFIG_SOC_USB_OTG_SUPPORTED && (CONFIG_TINYUSB_HID_COUNT || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_MIDI_COUNT || CONFIG_TINYUSB_CDC_ENABLED)

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbdevice.h>
#include <tactility/drivers/usb_cdc_device.h>
#include <tactility/drivers/usb_device_controller.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <tinyusb.h>
#include <tinyusb_default_config.h>
#include <tusb.h>

#include <cstdio>
#include <cstring>

#if CONFIG_IDF_TARGET_ESP32P4
#include <hal/usb_wrap_ll.h>
#include <soc/usb_wrap_struct.h>
#elif CONFIG_IDF_TARGET_ESP32S3
#include <hal/usb_serial_jtag_ll.h>
#endif

#define TAG "esp32_usb_device_controller"
#define GET_CONFIG(device) ((const Esp32UsbDeviceConfig*)(device)->config)

// Composite descriptor scratch buffer, sized for worst case (any one primary + CDC). Primary
// contributions today max out at MIDI's ~54 bytes (TUD_CONFIG_DESC_LEN + 2*9 + 2*9 + ...); 96
// leaves headroom without tracking each primary's exact size here.
static constexpr size_t COMPOSITE_CONFIG_DESCRIPTOR_MAX = TUD_CONFIG_DESC_LEN + 96 + TUD_CDC_DESC_LEN;
// Worst case: primary's own table (lang/mfr/product/serial/interface = 5) + CDC's own interface string.
static constexpr size_t COMPOSITE_STRING_DESCRIPTOR_MAX = 6;

// Detail for the most recent claim() failure - the console can be unreachable by the time this
// fails (route_phy_for_device_mode() already reassigned the PHY away from it), so callers read
// this instead of relying on the LOG_E above having been visible.
static char last_claim_error[48] = "";

extern "C" const char* usb_device_controller_get_last_error() {
    return last_claim_error;
}

// ---- Controller state ----
// One TinyUSB device-mode slot, shared by every USB device class (each a separate child device
// under usbdevice0 in the devicetree; \see esp32_usb_hid_device.cpp, esp32_usb_device_msc.cpp,
// esp32_usb_midi_device.cpp). Only one primary class may be installed at a time, enforced in
// claim(). CDC (esp32_usb_cdc_device.cpp) is a separate, orthogonal devicetree-presence addon
// composited into whichever primary is active; \see claim()'s cdc_enabled computation.

struct UsbDeviceControllerCtx {
    enum UsbDeviceClass active_class = USB_DEVICE_CLASS_NONE;
    bool phy_routed = false;
    bool cdc_console_started = false;

    // Allocation state, live from begin_claim() through claim()'s call to tinyusb_driver_install()
    // (or until the claim attempt is abandoned by a caller that never follows through with claim()).
    bool allocation_open = false;
    uint8_t next_interface = 0;
    uint8_t next_in_endpoint = 1;   // EP0 reserved
    uint8_t next_out_endpoint = 1;

    // Composite descriptor scratch, rebuilt on every claim(); can't be `static const` per-class
    // anymore now that CDC's presence is a runtime devicetree fact, not compile-time. Two buffers
    // since a primary (MSC) may need different bytes per speed (\see UsbInterfaceContribution's
    // hs_descriptor_bytes). Under !TUD_OPT_HIGH_SPEED only the fs buffer is ever used.
    uint8_t composite_fs_config_descriptor[COMPOSITE_CONFIG_DESCRIPTOR_MAX];
#if (TUD_OPT_HIGH_SPEED)
    uint8_t composite_hs_config_descriptor[COMPOSITE_CONFIG_DESCRIPTOR_MAX];
#endif
    const char* composite_string_descriptor[COMPOSITE_STRING_DESCRIPTOR_MAX];
    tusb_desc_device_t composite_device_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_desc_device_qualifier_t composite_device_qualifier;
#endif
    tinyusb_config_t composite_tusb_cfg;

    struct Device* cdc_child = nullptr; // resolved once in start_device(); nullptr if no usbdevicecdc0 node
};

static void route_phy_for_device_mode() {
#if CONFIG_IDF_TARGET_ESP32P4
    // Tab5's USB-C is wired to ESP32-P4 FSLS PHY0 (GPIO24/25). ESP-IDF's default USB_WRAP
    // route uses FSLS PHY1 (GPIO26/27), so switch it here before installing TinyUSB.
    usb_wrap_ll_phy_select(&USB_WRAP, 0);
#endif
}

static void restore_default_phy_route() {
#if CONFIG_IDF_TARGET_ESP32P4
    usb_wrap_ll_phy_select(&USB_WRAP, 1);
#elif CONFIG_IDF_TARGET_ESP32S3
    // S2/S3 share one FSLS PHY between USB-OTG and USB-Serial-JTAG. esp_tinyusb's teardown
    // leaves the PHY muxed to USB-OTG \afterward, so the native console never comes back
    // without this.
    //
    // \afterward ESP-IDF's usb_del_phy() (called from tinyusb_driver_uninstall() above).
    usb_serial_jtag_ll_phy_enable_external(false);
#endif
}

static bool find_cdc_child(struct Device* child, void* context) {
    if (device_get_type(child) == &USB_CDC_DEVICE_TYPE) {
        *static_cast<struct Device**>(context) = child;
        return false; // stop iterating
    }
    return true;
}

// Devicetree children are constructed/added/started strictly after their parent
// (kernel_init.cpp's dts_devices[] loop starts each device in list order, parent first), so the
// usbdevicecdc0 child does not exist yet when this controller's own start_device() runs.
// Discovering it lazily here (called from is_cdc_enabled(), well after all devicetree devices
// have finished starting) instead of once at start_device() time is the only way to actually see
// it. Cheap to re-scan every call: at most one child of this type per board.
static struct Device* find_cdc_child_lazy(struct Device* device, struct UsbDeviceControllerCtx* ctx) {
    if (ctx->cdc_child == nullptr) {
        device_for_each_child(device, &ctx->cdc_child, find_cdc_child);
    }
    return ctx->cdc_child;
}

// ---- Controller API ----

static error_t begin_claim(struct Device* device) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));
    if (ctx->active_class != USB_DEVICE_CLASS_NONE) {
        LOG_E(TAG, "begin_claim: slot busy (active_class=%d)", ctx->active_class);
        return ERROR_RESOURCE_BUSY;
    }
    ctx->allocation_open = true;
    ctx->next_interface = 0;
    ctx->next_in_endpoint = 1;
    ctx->next_out_endpoint = 1;
    return ERROR_NONE;
}

static error_t allocate_interfaces(struct Device* device, uint8_t interface_count,
                                    uint8_t in_endpoint_count, uint8_t out_endpoint_count,
                                    struct UsbInterfaceAllocation* out_allocation) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));
    if (!ctx->allocation_open) {
        LOG_E(TAG, "allocate_interfaces: called without a preceding begin_claim()");
        return ERROR_INVALID_STATE;
    }

    out_allocation->first_interface_number = ctx->next_interface;
    out_allocation->first_in_endpoint  = in_endpoint_count  > 0 ? (uint8_t)(0x80 | ctx->next_in_endpoint) : 0;
    out_allocation->first_out_endpoint = out_endpoint_count > 0 ? ctx->next_out_endpoint : 0;

    ctx->next_interface   = (uint8_t)(ctx->next_interface + interface_count);
    ctx->next_in_endpoint  = (uint8_t)(ctx->next_in_endpoint + in_endpoint_count);
    ctx->next_out_endpoint = (uint8_t)(ctx->next_out_endpoint + out_endpoint_count);
    return ERROR_NONE;
}

static bool is_cdc_enabled(struct Device* device) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));
    struct Device* cdc_child = find_cdc_child_lazy(device, ctx);
    return cdc_child != nullptr && usb_cdc_device_is_present(cdc_child);
}

static error_t claim(struct Device* device, enum UsbDeviceClass usb_class, const struct UsbDeviceClaimConfig* config) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));

    if (ctx->active_class != USB_DEVICE_CLASS_NONE) {
        LOG_E(TAG, "claim: slot busy (active_class=%d, requested=%d)", ctx->active_class, usb_class);
        return ERROR_RESOURCE_BUSY;
    }
    if (config == nullptr || config->device_descriptor == nullptr || config->primary.descriptor_bytes == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->allocation_open) {
        LOG_E(TAG, "claim: no begin_claim()/allocate_interfaces() session in progress");
        return ERROR_INVALID_STATE;
    }

    // MSC is deliberately excluded from CDC compositing as it doesn't work on Tab5/P4 devices.
    // It could be re-enabled for other architectures after more extensive future testing.
    const bool cdc_enabled = usb_class != USB_DEVICE_CLASS_MSC && is_cdc_enabled(device);

    // ---- String table: primary's table, plus CDC's own interface string appended last.
    size_t string_count = config->string_descriptor_count;
    if (string_count > COMPOSITE_STRING_DESCRIPTOR_MAX) {
        ctx->allocation_open = false;
        LOG_E(TAG, "claim: primary string table too large (%u > %u)", (unsigned)string_count, (unsigned)COMPOSITE_STRING_DESCRIPTOR_MAX);
        return ERROR_INVALID_ARGUMENT;
    }
    memcpy(ctx->composite_string_descriptor, config->string_descriptor, string_count * sizeof(const char*));

    struct UsbInterfaceContribution cdc_contribution = {};
    if (cdc_enabled) {
        const char* cdc_interface_string = nullptr;
        if (string_count >= COMPOSITE_STRING_DESCRIPTOR_MAX) {
            ctx->allocation_open = false;
            LOG_E(TAG, "claim: no room for CDC's interface string");
            return ERROR_INVALID_ARGUMENT;
        }
        error_t err = usb_cdc_device_build_contribution(ctx->cdc_child, device, (uint8_t)string_count,
                                                          &cdc_contribution, &cdc_interface_string);
        if (err != ERROR_NONE) {
            ctx->allocation_open = false;
            LOG_E(TAG, "claim: CDC build_contribution failed");
            return err;
        }
        ctx->composite_string_descriptor[string_count] = cdc_interface_string;
        string_count++;
    }

    // ---- Descriptor byte assembly: primary's bytes, then CDC's (if enabled), behind one config
    // header. Built twice (fs/hs) under TUD_OPT_HIGH_SPEED if the primary supplied distinct
    // high-speed bytes (\see UsbInterfaceContribution::hs_descriptor_bytes). CDC never varies by
    // speed, so its bytes are reused verbatim in both buffers.
    auto assemble = [&](uint8_t* dest, size_t dest_capacity, const uint8_t* primary_bytes, size_t primary_len) -> error_t {
        const size_t total_len = TUD_CONFIG_DESC_LEN + primary_len + (cdc_enabled ? cdc_contribution.descriptor_bytes_len : 0);
        if (total_len > dest_capacity) {
            LOG_E(TAG, "claim: composite descriptor too large (%u > %u)", (unsigned)total_len, (unsigned)dest_capacity);
            return ERROR_INVALID_ARGUMENT;
        }
        const uint8_t config_header[] = {
            TUD_CONFIG_DESCRIPTOR(1, ctx->next_interface, 0, total_len, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
        };
        uint8_t* p = dest;
        memcpy(p, config_header, sizeof(config_header));
        p += sizeof(config_header);
        memcpy(p, primary_bytes, primary_len);
        p += primary_len;
        if (cdc_enabled) {
            memcpy(p, cdc_contribution.descriptor_bytes, cdc_contribution.descriptor_bytes_len);
        }
        return ERROR_NONE;
    };

    error_t assemble_err = assemble(ctx->composite_fs_config_descriptor, sizeof(ctx->composite_fs_config_descriptor),
                                     config->primary.descriptor_bytes, config->primary.descriptor_bytes_len);
    if (assemble_err != ERROR_NONE) {
        ctx->allocation_open = false;
        return assemble_err;
    }
#if (TUD_OPT_HIGH_SPEED)
    const uint8_t* hs_primary_bytes = config->primary.hs_descriptor_bytes != nullptr
        ? config->primary.hs_descriptor_bytes : config->primary.descriptor_bytes;
    const size_t hs_primary_len = config->primary.hs_descriptor_bytes != nullptr
        ? config->primary.hs_descriptor_bytes_len : config->primary.descriptor_bytes_len;
    assemble_err = assemble(ctx->composite_hs_config_descriptor, sizeof(ctx->composite_hs_config_descriptor),
                             hs_primary_bytes, hs_primary_len);
    if (assemble_err != ERROR_NONE) {
        ctx->allocation_open = false;
        return assemble_err;
    }
#endif

    ctx->allocation_open = false;

    ctx->composite_device_descriptor = *static_cast<const tusb_desc_device_t*>(config->device_descriptor);
    if (cdc_enabled || usb_class == USB_DEVICE_CLASS_MSC) {
        ctx->composite_device_descriptor.bDeviceClass = TUSB_CLASS_MISC;
        ctx->composite_device_descriptor.bDeviceSubClass = MISC_SUBCLASS_COMMON;
        ctx->composite_device_descriptor.bDeviceProtocol = MISC_PROTOCOL_IAD;
    } else {
        ctx->composite_device_descriptor.bDeviceClass = TUSB_CLASS_UNSPECIFIED;
        ctx->composite_device_descriptor.bDeviceSubClass = 0x00;
        ctx->composite_device_descriptor.bDeviceProtocol = 0x00;
    }

    ctx->composite_tusb_cfg = {};
    ctx->composite_tusb_cfg.descriptor.device = &ctx->composite_device_descriptor;
    ctx->composite_tusb_cfg.descriptor.string = ctx->composite_string_descriptor;
    ctx->composite_tusb_cfg.descriptor.string_count = (int)string_count;
    ctx->composite_tusb_cfg.phy.skip_setup = false;
    ctx->composite_tusb_cfg.task = TINYUSB_TASK_DEFAULT(); // task.size/priority can't be left 0 - rejected by tinyusb_task_check_config()
#if (TUD_OPT_HIGH_SPEED)
    ctx->composite_tusb_cfg.descriptor.full_speed_config = ctx->composite_fs_config_descriptor;
    ctx->composite_tusb_cfg.descriptor.high_speed_config = ctx->composite_hs_config_descriptor;
    // The qualifier descriptor's class triad must mirror the device descriptor's (same IAD
    // reasoning). Built here rather than supplied per-primary since its content is boilerplate
    // (same bMaxPacketSize0/bNumConfigurations for every class) and the controller already knows
    // the class triad.
    ctx->composite_device_qualifier = {
        .bLength            = sizeof(tusb_desc_device_qualifier_t),
        .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = ctx->composite_device_descriptor.bDeviceClass,
        .bDeviceSubClass    = ctx->composite_device_descriptor.bDeviceSubClass,
        .bDeviceProtocol    = ctx->composite_device_descriptor.bDeviceProtocol,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
        .bNumConfigurations = 0x01,
        .bReserved          = 0x00,
    };
    ctx->composite_tusb_cfg.descriptor.qualifier = &ctx->composite_device_qualifier;
#else
    ctx->composite_tusb_cfg.descriptor.full_speed_config = ctx->composite_fs_config_descriptor;
#endif
    ctx->composite_tusb_cfg.phy.self_powered = false;
    ctx->composite_tusb_cfg.phy.vbus_monitor_io = 0;

    route_phy_for_device_mode();
    ctx->phy_routed = true;

    esp_err_t install_result = tinyusb_driver_install(&ctx->composite_tusb_cfg);
    if (install_result != ESP_OK) {
        LOG_E(TAG, "claim: tinyusb_driver_install failed for class %d: %s", usb_class, esp_err_to_name(install_result));
        snprintf(last_claim_error, sizeof(last_claim_error), "tinyusb_driver_install: %s", esp_err_to_name(install_result));
        if (ctx->phy_routed) {
            restore_default_phy_route();
            ctx->phy_routed = false;
        }
        return ERROR_RESOURCE;
    }

    ctx->active_class = usb_class;

    if (cdc_enabled) {
        if (usb_cdc_device_start_console(ctx->cdc_child) == ERROR_NONE) {
            ctx->cdc_console_started = true;
        } else {
            LOG_E(TAG, "claim: CDC start_console failed - continuing without console");
        }
    }

    return ERROR_NONE;
}

static error_t release(struct Device* device, enum UsbDeviceClass usb_class) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));

    if (ctx->active_class != usb_class) {
        LOG_E(TAG, "release: class %d does not hold the slot (active=%d)", usb_class, ctx->active_class);
        return ERROR_INVALID_STATE;
    }

    if (ctx->cdc_console_started) {
        usb_cdc_device_stop_console(ctx->cdc_child);
        ctx->cdc_console_started = false;
    }

    // Signal disconnect before tearing down so the host notices promptly.
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));

    tinyusb_driver_uninstall();

    if (ctx->phy_routed) {
        restore_default_phy_route();
        ctx->phy_routed = false;
    }

    ctx->active_class = USB_DEVICE_CLASS_NONE;
    return ERROR_NONE;
}

static enum UsbDeviceClass get_active_class(struct Device* device) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));
    return ctx->active_class;
}

extern const UsbDeviceControllerApi usb_device_controller_api = {
    .begin_claim         = begin_claim,
    .allocate_interfaces = allocate_interfaces,
    .claim               = claim,
    .release             = release,
    .get_active_class    = get_active_class,
    .is_cdc_enabled      = is_cdc_enabled,
};

// ---- Driver lifecycle ----
// This device is defined in each board's .dts (usbdevice0), same pattern as usbhost0. Its
// child classes (usbdevicehid0, usbdevicemsc0, usbdevicemidi0, usbdevicecdc0) are separate .dts
// nodes with their own drivers, wired to this device as their parent by the devicetree compiler.

extern "C" {

static error_t start_device(struct Device* device) {
    (void)GET_CONFIG(device); // no configuration - placeholder only
    auto* ctx = new UsbDeviceControllerCtx();
    device_set_driver_data(device, ctx);
    // cdc_child is NOT resolved here: usbdevicecdc0 (a child of this device in the devicetree)
    // hasn't been constructed/started yet at this point; kernel_init.cpp starts devicetree
    // devices in list order, parent before children.
    // \see find_cdc_child_lazy(), is_cdc_enabled() for the lazy resolution.
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbDeviceControllerCtx*>(device_get_driver_data(device));
    // Safety cleanup: if a class still holds the slot (e.g. this device is stopped while HID/
    // MSC/MIDI is still active), tear TinyUSB down and restore the PHY route before freeing ctx;
    // otherwise the installed TinyUSB driver and mis-routed PHY would outlive the context that
    // tracks them. Mirrors the same safety-release pattern each child driver's own stop_device uses.
    // \see esp32_usb_hid_device.cpp, esp32_usb_device_msc.cpp, esp32_usb_midi_device.cpp
    if (ctx->active_class != USB_DEVICE_CLASS_NONE) {
        release(device, ctx->active_class);
    }
    delete ctx;
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

Driver esp32_usb_device_controller_driver = {
    .name         = "esp32_usb_device_controller",
    .compatible   = (const char*[]) { "espressif,esp32-usbdevice", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = &usb_device_controller_api,
    .device_type  = &USB_DEVICE_CONTROLLER_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED && (CONFIG_TINYUSB_HID_COUNT || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_MIDI_COUNT)
