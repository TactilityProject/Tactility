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
 * @brief esp_hosted co-processor OTA API.
 *
 * Only meaningful on boards with a hosted co-processor (esp-hosted-mcu, e.g. P4+C6/C5). There is
 * exactly one co-processor per board, so unlike most kernel drivers this isn't bound to a
 * devicetree node or Device handle - it's a singleton capability, present or absent depending on
 * the platform build (mirrors how the underlying esp_hosted component itself is a singleton).
 *
 * Wraps the esp_hosted OTA/transport calls a firmware-update UI needs: waiting for the
 * co-processor's RPC transport to be ready, querying its running firmware version, and pushing +
 * activating a new image. Callers are responsible for validating the image (chip id, magic, etc.)
 * before calling esp_hosted_ota_begin() - this API only moves bytes and reports esp_hosted's own
 * result codes.
 */

/**
 * @brief Wait for the co-processor's RPC transport to be ready to accept OTA/custom-RPC calls.
 * @param[in] timeout_ms how long to wait
 * @return true if the transport is ready, false on timeout
 */
bool esp_hosted_ota_wait_for_transport(uint32_t timeout_ms);

/**
 * @brief Get the currently running co-processor firmware version.
 * @param[out] major major version
 * @param[out] minor minor version
 * @param[out] patch patch version
 * @return ERROR_NONE on success, or an error code (e.g. transport not up)
 */
error_t esp_hosted_ota_get_coprocessor_fwversion(uint32_t* major, uint32_t* minor, uint32_t* patch);

/**
 * @brief Get the connected co-processor's chip id and human-readable target name (e.g. "esp32c6").
 * @param[out] chip_id the raw chip id (ESP_PRIV_FIRMWARE_CHIP_* on the esp_hosted side)
 * @param[out] target_name buffer to receive the null-terminated target name
 * @param[in] target_name_len size of target_name, including room for the null terminator
 * @return ERROR_NONE on success, or an error code (e.g. transport not up)
 */
error_t esp_hosted_ota_get_cp_info(uint32_t* chip_id, char* target_name, size_t target_name_len);

/**
 * @brief Start an OTA transfer to the co-processor.
 * @return ERROR_NONE on success, or an error code
 */
error_t esp_hosted_ota_begin(void);

/**
 * @brief Write a chunk of firmware image data to the in-progress OTA transfer.
 * @param[in] data the chunk to write
 * @param[in] length the chunk length in bytes
 * @return ERROR_NONE on success, or an error code
 */
error_t esp_hosted_ota_write(const uint8_t* data, size_t length);

/**
 * @brief Finalize the OTA transfer. Must be called after all data has been written, whether or
 * not activation will follow.
 * @return ERROR_NONE on success, or an error code
 */
error_t esp_hosted_ota_end(void);

/**
 * @brief Activate the newly transferred firmware, causing the co-processor to reboot into it.
 * @note Only implemented by co-processor firmware >= v2.6.0 - callers must check
 * esp_hosted_ota_get_coprocessor_fwversion() against that before calling this, and fall back to
 * treating the transfer as complete after esp_hosted_ota_end() otherwise (requires a host restart
 * to resync either way).
 * @return ERROR_NONE on success, or an error code
 */
error_t esp_hosted_ota_activate(void);

/**
 * @brief Function table implementing the esp_hosted OTA API above.
 *
 * This API isn't bound to a Device (see the file-level comment for why), so unlike
 * tactility/drivers/wifi.h's WifiApi it can't be dispatched through a Device's driver vtable.
 * Instead the platform implementation (Platforms/platform-esp32) registers one of these at
 * startup via esp_hosted_ota_register(), and the plain functions above dispatch through it. On
 * platforms/boards with no hosted co-processor, nothing ever registers and the functions above
 * return ERROR_NOT_SUPPORTED / false - this file is always compiled and linked (needed so the ELF
 * app loader's kernel symbol table always has these symbols, matching how every other kernel
 * driver header is unconditionally available), it just has nothing to dispatch to.
 */
struct EspHostedOtaApi {
    bool (*wait_for_transport)(uint32_t timeout_ms);
    error_t (*get_coprocessor_fwversion)(uint32_t* major, uint32_t* minor, uint32_t* patch);
    error_t (*get_cp_info)(uint32_t* chip_id, char* target_name, size_t target_name_len);
    error_t (*begin)(void);
    error_t (*write)(const uint8_t* data, size_t length);
    error_t (*end)(void);
    error_t (*activate)(void);
};

/** @brief Register the platform implementation. Called once by platform-esp32 on hosted boards. */
void esp_hosted_ota_register(const struct EspHostedOtaApi* api);

#ifdef __cplusplus
}
#endif
