#ifdef ESP_PLATFORM

#include "Tactility/hal/usb/UsbTusb.h"
#include <Tactility/PartitionsEsp.h>

#include <sdkconfig.h>

#if CONFIG_TINYUSB_MSC_ENABLED == 1

#include <Tactility/Logger.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <tinyusb.h>
#include <tusb_msc_storage.h>
#include <wear_levelling.h>

#if CONFIG_IDF_TARGET_ESP32P4
#include "hal/usb_wrap_ll.h"
#endif

#define EPNUM_MSC 1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define SECTOR_SIZE 512

static const auto LOGGER = tt::Logger("USB");

namespace tt::hal::usb {
    extern sdmmc_card_t* getCard();
}

// Set when mass storage was started as part of the dedicated reboot-into-MSC boot flow.
// Used to decide whether ejecting the volume should automatically reboot back to normal OS.
static bool startedFromBootMode = false;

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT  = 0x01,
    EDPT_MSC_IN   = 0x81,
};

static bool driverInstalled = false;

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // TODO: Espressif VID. Do we need to change this?
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
    "Espressif",                    // 1: Manufacturer
    "Tactility Device",             // 2: Product
    "42",                           // 3: Serials
    "Tactility Mass Storage",       // 4. MSC
};

static uint8_t const msc_fs_configuration_desc[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

#if (TUD_OPT_HIGH_SPEED)
    static const tusb_desc_device_qualifier_t device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0
};

static uint8_t const msc_hs_configuration_desc[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 512),
};
#endif // TUD_OPT_HIGH_SPEED

static void storage_mount_changed_cb(tinyusb_msc_event_t* event) {
    if (event->mount_changed_data.is_mounted) {
        LOGGER.info("MSC Mounted");
        // Storage is only (re)mounted into our own filesystem after the host sends a SCSI
        // START STOP UNIT eject (see tud_msc_start_stop_cb() in tusb_msc_storage.c). Windows
        // is known not to send this reliably, so this is a best-effort path for hosts that do
        // (e.g. Linux/macOS) - the "Return to OS" button on the boot screen is the primary one.
        // If we got here while booted into MSC mode, it's safe to reboot back into normal OS now.
        if (startedFromBootMode) {
            LOGGER.info("MSC ejected by host, rebooting into normal OS");
            esp_restart();
        }
    } else {
        LOGGER.info("MSC Unmounted");
    }
}

static bool ensureDriverInstalled() {
    if (driverInstalled) {
        return true;
    }

#if CONFIG_IDF_TARGET_ESP32P4
    // Tab5's USB-C port is wired to FSLS PHY0, but the USB_WRAP (FS/FSLS) controller
    // used by TinyUSB device mode defaults to FSLS PHY1. Route it to PHY0 before
    // installing the TinyUSB driver.
    usb_wrap_ll_phy_select(&USB_WRAP, 0);
#endif

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = msc_fs_configuration_desc,
        .hs_configuration_descriptor = msc_hs_configuration_desc,
        .qualifier_descriptor = &device_qualifier,
#else
        .configuration_descriptor = msc_fs_configuration_desc,
#endif // TUD_OPT_HIGH_SPEED
        .self_powered = false,
        .vbus_monitor_io = 0
    };

    if (tinyusb_driver_install(&tusb_cfg) != ESP_OK) {
        LOGGER.error("Failed to install TinyUSB driver");
#if CONFIG_IDF_TARGET_ESP32P4
        // Roll back routing when TinyUSB did not start.
        usb_wrap_ll_phy_select(&USB_WRAP, 1);
#endif
        return false;
    }

    driverInstalled = true;
    return true;
}

bool tusbIsSupported() { return true; }

bool tusbStartMassStorageWithSdmmc(bool fromBootMode) {
    if (!ensureDriverInstalled()) {
        return false;
    }
    startedFromBootMode = fromBootMode;

    auto* card = tt::hal::usb::getCard();
    if (card == nullptr) {
        LOGGER.error("SD card not mounted");
        return false;
    }

    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = card,
        .callback_mount_changed = storage_mount_changed_cb,
        .callback_premount_changed = nullptr,
        .mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = false,
            .use_one_fat = false
        }
    };

    auto result = tinyusb_msc_storage_init_sdmmc(&config_sdmmc);
    if (result != ESP_OK) {
        LOGGER.error("TinyUSB SDMMC init failed: {}", esp_err_to_name(result));
    } else {
        LOGGER.info("TinyUSB SDMMC init success");
    }

    return result == ESP_OK;
}

bool tusbStartMassStorageWithFlash() {
    LOGGER.info("Starting flash MSC");
    if (!ensureDriverInstalled()) {
        return false;
    }
    startedFromBootMode = true;

    wl_handle_t handle = tt::getDataPartitionWlHandle();
    if (handle == WL_INVALID_HANDLE) {
        LOGGER.error("WL not mounted for /data");
        return false;
    }

    const tinyusb_msc_spiflash_config_t config_flash = {
        .wl_handle = handle,
        .callback_mount_changed = storage_mount_changed_cb,
        .callback_premount_changed = nullptr,
        .mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = false,
            .use_one_fat = false
        }
    };

    esp_err_t result = tinyusb_msc_storage_init_spiflash(&config_flash);
    if (result != ESP_OK) {
        LOGGER.error("TinyUSB flash init failed: {}", esp_err_to_name(result));
    } else {
        LOGGER.info("TinyUSB flash init success");
    }
    return result == ESP_OK;
}

void tusbStop() {
    // Actively signal a disconnect to the host before tearing down the peripheral, otherwise
    // a subsequent esp_restart() resets the chip too fast for the host to notice the device
    // went away, leaving it stuck showing the old MSC device until the cable is replugged.
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));

    tinyusb_msc_storage_deinit();
#if CONFIG_IDF_TARGET_ESP32P4
    usb_wrap_ll_phy_select(&USB_WRAP, 1);
#endif
}

bool tusbCanStartMassStorageWithFlash() {
    return tusbIsSupported() && (tt::getDataPartitionWlHandle() != WL_INVALID_HANDLE);
}

#else

bool tusbIsSupported() { return false; }
bool tusbStartMassStorageWithSdmmc() { return false; }
bool tusbStartMassStorageWithFlash() { return false; }
void tusbStop() {}
bool tusbCanStartMassStorageWithFlash() { return false; }

#endif // CONFIG_TINYUSB_MSC_ENABLED

#endif // ESP_PLATFORM
