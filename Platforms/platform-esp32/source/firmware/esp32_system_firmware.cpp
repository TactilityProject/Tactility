// SPDX-License-Identifier: Apache-2.0
#include <tactility/firmware/esp32_system_firmware.h>

#include <tactility/error_esp32.h>
#include <tactility/firmware/firmware.h>
#include <tactility/log.h>

#include <esp_chip_info.h>
#include <esp_ota_ops.h>
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
};

} // namespace

extern "C" error_t esp32_system_firmware_register(void) {
    if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    return firmware_register(FIRMWARE_ID_SYSTEM, &systemFirmwareOps, nullptr);
}
