#include <sdkconfig.h>
#if CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MSC_ENABLED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbdevice.h>
#include <tactility/drivers/usb_device_controller.h>
#include <tactility/drivers/usb_msc_device.h>
#include <tactility/log.h>

#include <tinyusb.h>
#include <tinyusb_msc.h>
#include <wear_levelling.h>

#include <cstdio>
#include <cstring>

#define TAG "esp32_usb_device_msc"
#define GET_CONFIG(device) ((const Esp32UsbDeviceChildConfig*)(device)->config)

// Detail for the most recent msc_device_start() failure - the console can be unreachable by the
// time this fails (the controller already reassigned the PHY away from it), so callers read this
// instead of relying on the LOG_E calls below having been visible.
static char last_msc_error[48] = "";

extern "C" const char* usb_msc_device_get_last_error() {
    return last_msc_error;
}

// ---- MSC device descriptor set ----

// Mutable, matching the other primary classes' descriptors, even though MSC's own
// bDeviceClass/SubClass/Protocol below are never actually rewritten by the controller - MSC is
// excluded from CDC compositing (see esp32_usb_device_controller.cpp's claim(), cdc_enabled
// computation) and always keeps its own unconditional MISC/IAD triad regardless.
static tusb_desc_device_t msc_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A, // Espressif VID
    .idProduct          = 0x4002,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

static const char* msc_string_descriptor[] = {
    (const char[]) { 0x09, 0x04 },  // 0: English (0x0409)
    "Tactility",                    // 1: Manufacturer
    "Tactility Device",             // 2: Product
    "42",                           // 3: Serial
    "Tactility Mass Storage",       // 4: MSC
};

// Interface/endpoint numbers here are the values usb_device_controller_allocate_interfaces()
// always returns for MSC in practice (MSC is always the first/only allocation for itself, per
// the controller's fixed primary-first ordering) - still requested via allocate_interfaces() at
// claim time rather than assumed, so this stays correct if that ordering ever changes.
static uint8_t msc_fs_configuration_descriptor[TUD_MSC_DESC_LEN];
#if (TUD_OPT_HIGH_SPEED)
static uint8_t msc_hs_configuration_descriptor[TUD_MSC_DESC_LEN];
#endif

// ---- MSC device state ----

struct UsbMscDeviceCtx {
    UsbMscDeviceMountChangedCallback mount_changed_cb = nullptr;
    void* mount_changed_context = nullptr;
    tinyusb_msc_storage_handle_t storage_handle = nullptr;
    bool storage_active = false;
};

// device pointer isn't threaded through the TinyUSB mount-changed callback, so this driver
// supports a single active MSC device instance at a time - matches the single TinyUSB
// device-mode slot the controller already enforces.
static struct Device* active_msc_device = nullptr;

// esp_tinyusb 2.x's MOUNT_COMPLETE event fires for either owner taking the storage. Kernel-facing
// UsbMscDeviceMountChangedCallback's `mounted` (\see usb_msc_device.h) means "mounted into our own
// filesystem" (the pre-2.x meaning, kept for callers like the boot-into-MSC flow that reboot back
// to normal OS once mounted==true) - i.e. ownership has passed to TINYUSB_MSC_STORAGE_MOUNT_APP,
// the opposite of the initial USB-owned state this driver starts storage in.
static void storage_mount_changed_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t* event, void* arg) {
    (void)handle;
    (void)arg;
    if (event->id != TINYUSB_MSC_EVENT_MOUNT_COMPLETE) return;
    if (active_msc_device == nullptr) return;
    auto* ctx = static_cast<UsbMscDeviceCtx*>(device_get_driver_data(active_msc_device));
    if (ctx == nullptr) return;

    const bool mounted = event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP;
    LOG_I(TAG, "%s", mounted ? "MSC mounted" : "MSC unmounted");
    if (ctx->mount_changed_cb != nullptr) {
        ctx->mount_changed_cb(mounted, ctx->mount_changed_context);
    }
}

// ---- MSC device API ----

static error_t msc_device_start(struct Device* device, enum UsbMscDeviceSource source, void* source_handle,
                                UsbMscDeviceMountChangedCallback mount_changed_cb, void* context) {
    if (source_handle == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* controller = device_get_parent(device);

    error_t begin_result = usb_device_controller_begin_claim(controller);
    if (begin_result != ERROR_NONE) {
        return begin_result;
    }

    struct UsbInterfaceAllocation alloc;
    error_t alloc_result = usb_device_controller_allocate_interfaces(controller, /*interface_count=*/1,
                                                                       /*in_endpoint_count=*/1, /*out_endpoint_count=*/1, &alloc);
    if (alloc_result != ERROR_NONE) {
        return alloc_result;
    }

    const uint8_t msc_fs_bytes[] = {
        TUD_MSC_DESCRIPTOR(alloc.first_interface_number, 4, alloc.first_out_endpoint, alloc.first_in_endpoint, 64),
    };
    memcpy(msc_fs_configuration_descriptor, msc_fs_bytes, sizeof(msc_fs_bytes));
#if (TUD_OPT_HIGH_SPEED)
    const uint8_t msc_hs_bytes[] = {
        TUD_MSC_DESCRIPTOR(alloc.first_interface_number, 4, alloc.first_out_endpoint, alloc.first_in_endpoint, 512),
    };
    memcpy(msc_hs_configuration_descriptor, msc_hs_bytes, sizeof(msc_hs_bytes));
#endif

    struct UsbDeviceClaimConfig claim_config = {};
    claim_config.device_descriptor = &msc_device_descriptor;
    claim_config.string_descriptor = msc_string_descriptor;
    claim_config.string_descriptor_count = sizeof(msc_string_descriptor) / sizeof(msc_string_descriptor[0]);
    claim_config.primary.descriptor_bytes = msc_fs_configuration_descriptor;
    claim_config.primary.descriptor_bytes_len = sizeof(msc_fs_configuration_descriptor);
#if (TUD_OPT_HIGH_SPEED)
    claim_config.primary.hs_descriptor_bytes = msc_hs_configuration_descriptor;
    claim_config.primary.hs_descriptor_bytes_len = sizeof(msc_hs_configuration_descriptor);
#endif
    claim_config.primary.interface_count = 1;
    claim_config.primary.in_endpoint_count = 1;
    claim_config.primary.out_endpoint_count = 1;

    error_t claim_result = usb_device_controller_claim(controller, USB_DEVICE_CLASS_MSC, &claim_config);
    if (claim_result != ERROR_NONE) {
        snprintf(last_msc_error, sizeof(last_msc_error), "claim: %s", usb_device_controller_get_last_error());
        return claim_result;
    }

    auto* ctx = static_cast<UsbMscDeviceCtx*>(device_get_driver_data(device));
    ctx->mount_changed_cb = mount_changed_cb;
    ctx->mount_changed_context = context;
    active_msc_device = device;

    // esp_tinyusb 2.x auto-remounts storage on every tud_mount_cb()/tud_umount_cb(), including
    // the bus resets that happen as a normal part of USB enumeration - not just a genuine host
    // eject. Left enabled, that fires a spurious "mounted into our own fs" transition almost
    // immediately after claim(), which callers like the boot-into-MSC flow read as "the host is
    // done with it" and act on right away. Disabling it restores the old (pre-2.x) behavior:
    // mount_point only changes via an explicit host SCSI eject or our own start/stop calls.
    const tinyusb_msc_driver_config_t driver_cfg = {
        .user_flags = { .val = 1 }, // auto_mount_off
        .callback = storage_mount_changed_cb,
        .callback_arg = nullptr,
    };
    esp_err_t result = tinyusb_msc_install_driver(&driver_cfg);
    if (result != ESP_OK) {
        LOG_E(TAG, "MSC driver install failed: %s", esp_err_to_name(result));
        snprintf(last_msc_error, sizeof(last_msc_error), "install_driver: %s", esp_err_to_name(result));
        active_msc_device = nullptr;
        usb_device_controller_release(controller, USB_DEVICE_CLASS_MSC);
        return ERROR_RESOURCE;
    }

    const tinyusb_msc_fatfs_config_t fat_fs_config = {
        .base_path = nullptr, // use CONFIG_TINYUSB_MSC_MOUNT_PATH default
        .config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = false,
            .use_one_fat = false,
        },
        .do_not_format = false,
        .format_flags = 0, // FM_ANY
    };

    tinyusb_msc_storage_handle_t handle = nullptr;
    if (source == USB_MSC_DEVICE_SOURCE_SDMMC) {
        const tinyusb_msc_storage_config_t config_sdmmc = {
            .medium = { .card = static_cast<sdmmc_card_t*>(source_handle) },
            .fat_fs = fat_fs_config,
            .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        };
        result = tinyusb_msc_new_storage_sdmmc(&config_sdmmc, &handle);
    } else {
        const tinyusb_msc_storage_config_t config_flash = {
            .medium = { .wl_handle = *static_cast<wl_handle_t*>(source_handle) },
            .fat_fs = fat_fs_config,
            .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        };
        result = tinyusb_msc_new_storage_spiflash(&config_flash, &handle);
    }

    if (result != ESP_OK) {
        LOG_E(TAG, "storage init failed: %s", esp_err_to_name(result));
        snprintf(last_msc_error, sizeof(last_msc_error), "new_storage: %s", esp_err_to_name(result));
        tinyusb_msc_uninstall_driver();
        active_msc_device = nullptr;
        usb_device_controller_release(controller, USB_DEVICE_CLASS_MSC);
        return ERROR_RESOURCE;
    }

    ctx->storage_handle = handle;
    ctx->storage_active = true;
    return ERROR_NONE;
}

static error_t msc_device_stop(struct Device* device) {
    auto* ctx = static_cast<UsbMscDeviceCtx*>(device_get_driver_data(device));
    if (ctx == nullptr || !ctx->storage_active) {
        return ERROR_NONE;
    }

    // Disconnect and fully stop the TinyUSB device task before freeing storage state: the task
    // keeps servicing SCSI commands (Windows polls TEST UNIT READY continuously, even on an
    // ejected-but-still-enumerated device) until tinyusb_driver_uninstall() actually stops it, so
    // freeing first left tud_msc_*_cb() callbacks dereferencing an already-freed storage handle.
    auto* controller = device_get_parent(device);
    usb_device_controller_release(controller, USB_DEVICE_CLASS_MSC);

    tinyusb_msc_delete_storage(ctx->storage_handle);
    tinyusb_msc_uninstall_driver();

    ctx->storage_handle = nullptr;
    ctx->storage_active = false;
    ctx->mount_changed_cb = nullptr;
    ctx->mount_changed_context = nullptr;
    if (active_msc_device == device) {
        active_msc_device = nullptr;
    }
    return ERROR_NONE;
}

static bool msc_device_is_connected(struct Device* device) {
    auto* ctx = static_cast<UsbMscDeviceCtx*>(device_get_driver_data(device));
    return ctx != nullptr && ctx->storage_active && tud_mounted();
}

extern const UsbMscDeviceApi esp32_usb_msc_device_api = {
    .start        = msc_device_start,
    .stop         = msc_device_stop,
    .is_connected = msc_device_is_connected,
};

// ---- Driver lifecycle ----
// Defined in each board's .dts as a child of usbdevice0 (e.g. usbdevicemsc0).

extern "C" {

static error_t start_device(struct Device* device) {
    (void)GET_CONFIG(device); // no configuration - placeholder only
    auto* ctx = new UsbMscDeviceCtx();
    device_set_driver_data(device, ctx);
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbMscDeviceCtx*>(device_get_driver_data(device));
    if (ctx != nullptr && ctx->storage_active) {
        msc_device_stop(device);
    }
    delete ctx;
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

Driver esp32_usb_msc_device_driver = {
    .name         = "esp32_usb_msc_device",
    .compatible   = (const char*[]) { "espressif,esp32-usbdevice-msc", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = &esp32_usb_msc_device_api,
    .device_type  = &USB_MSC_DEVICE_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MSC_ENABLED
