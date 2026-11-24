// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_sdcard.h>
#include <tactility/drivers/esp32_sdspi.h>
#include <tactility/drivers/spi_controller.h>

#include <sdmmc_cmd.h>

#include <tactility/log.h>

#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <tactility/drivers/esp32_sdmmc.h>
#endif

extern "C" {

sdmmc_card_t* esp32_sdcard_get_card(Device* device) {
    auto* driver = device_get_driver(device);
    if (driver_is_compatible(driver, "espressif,esp32-sdspi")) {
        return esp32_sdspi_get_card(device);
    }
#if SOC_SDMMC_HOST_SUPPORTED
    if (driver_is_compatible(driver, "espressif,esp32-sdmmc")) {
        return esp32_sdmmc_get_card(device);
    }
#endif
    return nullptr;
}

// No board in the tree has more than one SD-over-SPI card.
static constexpr size_t MAX_BUS_LOCKS = 2;

struct SdcardBusLock {
    Device* sdcard = nullptr;      // owner, for removal
    Device* controller = nullptr;  // parent SPI controller
    int slot = 0;                  // sdspi_dev_handle_t, read back from card->host.slot
    esp_err_t (*inner)(int, sdmmc_command_t*) = nullptr;
};

static SdcardBusLock bus_locks[MAX_BUS_LOCKS];

static SdcardBusLock* find_by_slot(int slot) {
    for (auto& entry : bus_locks) {
        if (entry.sdcard != nullptr && entry.slot == slot) {
            return &entry;
        }
    }
    return nullptr;
}

#define TAG "esp32_sdcard"

static esp_err_t locked_do_transaction(int slot, sdmmc_command_t* cmd) {
    const SdcardBusLock* entry = find_by_slot(slot);
    if (entry == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    spi_controller_lock(entry->controller);
    esp_err_t result = entry->inner(slot, cmd);
    spi_controller_unlock(entry->controller);
    return result;
}

error_t esp32_sdcard_install_bus_lock(Device* device, sdmmc_card_t* card) {
    auto* parent = device_get_parent(device);
    if (parent == nullptr || device_get_type(parent) != &SPI_CONTROLLER_TYPE) {
        return ERROR_NONE;
    }

    if (card == nullptr) {
        return ERROR_INVALID_STATE;
    }

    SdcardBusLock* slot_entry = nullptr;
    for (auto& entry : bus_locks) {
        if (entry.sdcard == nullptr) {
            slot_entry = &entry;
            break;
        }
    }
    if (slot_entry == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    slot_entry->sdcard = device;
    slot_entry->controller = parent;
    slot_entry->slot = card->host.slot;
    slot_entry->inner = card->host.do_transaction;
    card->host.do_transaction = locked_do_transaction;
    return ERROR_NONE;
}

void esp32_sdcard_remove_bus_lock(Device* device) {
    for (auto& entry : bus_locks) {
        if (entry.sdcard == device) {
            auto* card = esp32_sdcard_get_card(device);
            if (card != nullptr) {
                card->host.do_transaction = entry.inner;
            }
            entry = SdcardBusLock{};
            return;
        }
    }
}

}
