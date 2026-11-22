// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_sdcard.h>
#include <tactility/drivers/esp32_sdspi.h>
#include <tactility/drivers/spi_controller.h>

#include <sd_protocol_defs.h>
#include <sdmmc_cmd.h>

#include <esp_timer.h>
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

/** Totals of the SD commands, logged at debug level every 2 seconds while the card is in use. Guarded by the bus lock. */
struct CommandStatistics {
    uint32_t commands = 0;
    uint32_t single_reads = 0;
    uint32_t multi_reads = 0;
    uint32_t bytes = 0;
    uint32_t opcodes[64] = {};
    int64_t wait_us = 0;
    int64_t transfer_us = 0;
    int64_t max_wait_us = 0;
    int64_t since_us = 0;
};

static CommandStatistics statistics;

static void record_command(const sdmmc_command_t* cmd, int64_t wait_us, int64_t transfer_us) {
    const int64_t now = esp_timer_get_time();
    if (statistics.commands == 0) {
        statistics.since_us = now;
    }
    statistics.commands++;
    statistics.single_reads += (cmd->opcode == MMC_READ_BLOCK_SINGLE) ? 1 : 0;
    statistics.multi_reads += (cmd->opcode == MMC_READ_BLOCK_MULTIPLE) ? 1 : 0;
    statistics.bytes += cmd->datalen;
    statistics.opcodes[cmd->opcode & 63]++;
    statistics.wait_us += wait_us;
    statistics.transfer_us += transfer_us;
    statistics.max_wait_us = (wait_us > statistics.max_wait_us) ? wait_us : statistics.max_wait_us;
    if (now - statistics.since_us < 2000000) {
        return;
    }
    LOG_D(TAG, "%u commands (%u single reads, %u multi reads, %u KB) in %u ms: waited %u ms for the bus (max %u ms), transfers %u ms",
        (unsigned)statistics.commands, (unsigned)statistics.single_reads, (unsigned)statistics.multi_reads, (unsigned)(statistics.bytes / 1024),
        (unsigned)((now - statistics.since_us) / 1000), (unsigned)(statistics.wait_us / 1000), (unsigned)(statistics.max_wait_us / 1000),
        (unsigned)(statistics.transfer_us / 1000));
    char histogram[160] = {};
    size_t used = 0;
    for (uint32_t opcode = 0; opcode < 64 && used < sizeof(histogram); opcode++) {
        if (statistics.opcodes[opcode] != 0) {
            used += (size_t)snprintf(histogram + used, sizeof(histogram) - used, " cmd%u=%u", (unsigned)opcode, (unsigned)statistics.opcodes[opcode]);
        }
    }
    LOG_D(TAG, "Commands by opcode:%s", histogram);
    statistics = CommandStatistics {};
}

static esp_err_t locked_do_transaction(int slot, sdmmc_command_t* cmd) {
    const SdcardBusLock* entry = find_by_slot(slot);
    if (entry == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t start = esp_timer_get_time();
    spi_controller_lock(entry->controller);
    const int64_t locked = esp_timer_get_time();
    esp_err_t result = entry->inner(slot, cmd);
    record_command(cmd, locked - start, esp_timer_get_time() - locked);
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
