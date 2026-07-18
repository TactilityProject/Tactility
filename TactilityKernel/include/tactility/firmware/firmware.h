// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generic co-processor/companion-radio firmware info & update interface.
 *
 * Not tied to any particular driver: some devices are backed by a separate co-processor with its
 * own updatable firmware (e.g. esp-hosted-mcu's P4+C6/C5 pairing, reachable via
 * WifiApi::get_firmware_ops() in tactility/drivers/wifi.h); most aren't. A driver with no such
 * co-processor returns ERROR_NOT_SUPPORTED from whatever function exposes this interface
 * (leaving its output params untouched) - callers must check the result before using any
 * FirmwareOps function.
 */
struct FirmwareInfo {
    char name[32]; // human-readable target name, e.g. "esp32c6" - empty if unavailable
    uint32_t hw_id; // raw hardware/chip id, implementation-defined
    uint32_t fw_major;
    uint32_t fw_minor;
    uint32_t fw_patch;
};

struct FirmwareUpdateRequest {
    size_t image_size;
    uint32_t flags;
};

struct FirmwareUpdateHandle;

struct FirmwareOps {
    /**
     * Wait for the co-processor to be ready to accept firmware info/update calls.
     * @param[in] ctx opaque context, passed to every other FirmwareOps function
     * @param[in] timeout_ms how long to wait
     * @return true if ready, false on timeout
     */
    bool (*wait_ready)(void* ctx, uint32_t timeout_ms);

    /**
     * Get the co-processor's current firmware/hardware info.
     * @return ERROR_NONE on success, or an error code (e.g. not ready)
     */
    error_t (*get_info)(void* ctx, struct FirmwareInfo* info);

    /**
     * Begin a firmware update, filling in *handle for use with write()/finish()/abort().
     * @return ERROR_NONE on success, or an error code
     */
    error_t (*begin)(void* ctx, const struct FirmwareUpdateRequest* req, struct FirmwareUpdateHandle** handle);

    /** Write a chunk of firmware image data. @return ERROR_NONE on success, or an error code */
    error_t (*write)(struct FirmwareUpdateHandle* handle, const void* data, size_t len);

    /** Finalize a successful update. @return ERROR_NONE on success, or an error code */
    error_t (*finish)(struct FirmwareUpdateHandle* handle);

    /** Abort an in-progress update (e.g. after a write() failure). */
    error_t (*abort)(struct FirmwareUpdateHandle* handle);

    /**
     * Activate the most recently finish()ed firmware, causing the co-processor to reboot into it.
     * @note Not guaranteed to be supported by every co-processor firmware version - callers should
     * check get_info()'s reported version against their own known-good threshold before calling
     * this, and fall back to treating the update as complete after finish() otherwise.
     */
    error_t (*activate)(void* ctx);
};

#ifdef __cplusplus
}
#endif
