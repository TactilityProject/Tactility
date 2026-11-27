// SPDX-License-Identifier: Apache-2.0
#include <tactility/firmware/esp32_system_firmware.h>

#include <tactility/error_esp32.h>
#include <tactility/firmware/firmware.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <sdkconfig.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#define TAG "esp32_system_firmware"

struct FirmwareUpdateHandle {
    esp_ota_handle_t ota_handle;
    const esp_partition_t* partition;
};

namespace {

/** One update at a time: the handle is only valid while sessionActive is set */
FirmwareUpdateHandle session {};
std::atomic<bool> sessionActive { false };
/** The partition of the last finished update, which activate() boots from */
const esp_partition_t* finishedPartition = nullptr;
/** The MD5 of the partition table as hex, empty when the table has none */
char layoutId[33] = "";

constexpr size_t PARTITION_ENTRY_SIZE = 32;
constexpr uint8_t MD5_ENTRY_MAGIC = 0xEB;

/** Reads the MD5 that the partition table stores of its own entries */
void readLayoutId() {
    uint8_t entry[PARTITION_ENTRY_SIZE];
    for (size_t offset = 0; offset < ESP_PARTITION_TABLE_MAX_LEN; offset += PARTITION_ENTRY_SIZE) {
        if (esp_flash_read(nullptr, entry, CONFIG_PARTITION_TABLE_OFFSET + offset, sizeof(entry)) != ESP_OK) {
            return;
        }
        if (entry[0] == MD5_ENTRY_MAGIC && entry[1] == MD5_ENTRY_MAGIC) {
            for (size_t i = 0; i < 16; i++) {
                snprintf(layoutId + i * 2, 3, "%02x", entry[16 + i]);
            }
            return;
        }
        if (entry[0] == 0xFF && entry[1] == 0xFF) {
            return;
        }
    }
}

/** A new image runs once on trial: it only stays when it boots completely, otherwise the bootloader returns to the previous one */
void onBootCompleted(SystemEvent* /*event*/, void* /*context*/) {
    esp_ota_img_states_t imageState;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &imageState) == ESP_OK && imageState == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result == ESP_OK) {
            LOG_I(TAG, "The update booted, so it is kept");
        } else {
            LOG_E(TAG, "Failed to keep the update: %s", esp_err_to_name(result));
        }
    }
}

bool waitReady(void* /*ctx*/, uint32_t /*timeoutMs*/) {
    return true;
}

error_t getInfo(void* /*ctx*/, FirmwareInfo* info) {
    *info = {};
    strncpy(info->name, CONFIG_IDF_TARGET, sizeof(info->name) - 1);
    esp_chip_info_t chip {};
    esp_chip_info(&chip);
    info->hw_id = static_cast<uint32_t>(chip.model);
    unsigned major = 0, minor = 0, patch = 0;
    sscanf(TT_VERSION, "%u.%u.%u", &major, &minor, &patch);
    info->fw_major = major;
    info->fw_minor = minor;
    info->fw_patch = patch;
    return ERROR_NONE;
}

error_t getLayout(void* /*ctx*/, FirmwareLayout* layout) {
    *layout = {};
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    layout->max_image_size = partition != nullptr ? partition->size : 0;
    strcpy(layout->layout_id, layoutId);
    return ERROR_NONE;
}

error_t beginUpdate(void* /*ctx*/, const FirmwareUpdateRequest* request, FirmwareUpdateHandle** handle) {
    bool expected = false;
    if (!sessionActive.compare_exchange_strong(expected, true)) {
        return ERROR_RESOURCE_BUSY;
    }
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr) {
        sessionActive = false;
        return ERROR_NOT_SUPPORTED;
    }
    if (request->image_size > partition->size) {
        LOG_E(TAG, "Image of %u bytes doesn't fit partition %s of %lu bytes", static_cast<unsigned>(request->image_size), partition->label, static_cast<unsigned long>(partition->size));
        sessionActive = false;
        return ERROR_OUT_OF_RANGE;
    }
    const size_t size = request->image_size > 0 ? request->image_size : OTA_SIZE_UNKNOWN;
    esp_ota_handle_t otaHandle = 0;
    const esp_err_t result = esp_ota_begin(partition, size, &otaHandle);
    if (result != ESP_OK) {
        LOG_E(TAG, "esp_ota_begin failed: %s", esp_err_to_name(result));
        // A failed erase can leave the handle registered, which blocks the next begin until a reboot
        if (otaHandle != 0) {
            esp_ota_abort(otaHandle);
        }
        sessionActive = false;
        return esp_err_to_error(result);
    }
    LOG_I(TAG, "Writing update to %s", partition->label);
    session = { otaHandle, partition };
    *handle = &session;
    return ERROR_NONE;
}

error_t writeUpdate(FirmwareUpdateHandle* handle, const void* data, size_t length) {
    const esp_err_t result = esp_ota_write(handle->ota_handle, data, length);
    if (result != ESP_OK) {
        LOG_E(TAG, "esp_ota_write failed: %s", esp_err_to_name(result));
        return esp_err_to_error(result);
    }
    return ERROR_NONE;
}

error_t finishUpdate(FirmwareUpdateHandle* handle) {
    // esp_ota_end() validates the image and frees the handle, also when it fails
    const esp_err_t result = esp_ota_end(handle->ota_handle);
    if (result == ESP_OK) {
        finishedPartition = handle->partition;
    } else {
        LOG_E(TAG, "esp_ota_end failed: %s", esp_err_to_name(result));
    }
    sessionActive = false;
    return esp_err_to_error(result);
}

error_t abortUpdate(FirmwareUpdateHandle* handle) {
    const esp_err_t result = esp_ota_abort(handle->ota_handle);
    sessionActive = false;
    return esp_err_to_error(result);
}

error_t activate(void* /*ctx*/) {
    if (finishedPartition == nullptr) {
        return ERROR_INVALID_STATE;
    }
    const esp_err_t result = esp_ota_set_boot_partition(finishedPartition);
    if (result != ESP_OK) {
        LOG_E(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(result));
    }
    return esp_err_to_error(result);
}

const FirmwareOps systemFirmwareOps = {
    .wait_ready = waitReady,
    .get_info = getInfo,
    .begin = beginUpdate,
    .write = writeUpdate,
    .finish = finishUpdate,
    .abort = abortUpdate,
    .activate = activate,
    .get_layout = getLayout,
};

} // namespace

extern "C" error_t esp32_system_firmware_register(void) {
    if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
    if (invalid != nullptr) {
        LOG_W(TAG, "The firmware in %s didn't boot, so the previous one was restored", invalid->label);
    }
    readLayoutId();
    system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, onBootCompleted, nullptr);
    return firmware_register(FIRMWARE_ID_SYSTEM, &systemFirmwareOps, nullptr);
}
