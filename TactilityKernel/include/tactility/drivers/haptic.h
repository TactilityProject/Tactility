// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief API for haptic motor driver chips (e.g. ERM/LRA drivers with an onboard waveform
 * library, such as the TI DRV260x family).
 */
struct HapticApi {
    /**
     * @brief Sets one slot of the effect sequence played by start_playback().
     * @param[in] device the haptic device
     * @param[in] slot sequence slot index, driver-defined range (e.g. [0,7] for DRV260x)
     * @param[in] waveform waveform library index to play in this slot, 0 ends the sequence
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_waveform)(struct Device* device, uint8_t slot, uint8_t waveform);

    /**
     * @brief Selects the onboard waveform library to play effects from.
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*select_library)(struct Device* device, uint8_t library);

    /**
     * @brief Starts playback of the currently configured effect sequence.
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*start_playback)(struct Device* device);

    /**
     * @brief Stops playback.
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*stop_playback)(struct Device* device);
};

/**
 * @brief Sets one slot of the effect sequence using the specified haptic device.
 */
error_t haptic_set_waveform(struct Device* device, uint8_t slot, uint8_t waveform);

/**
 * @brief Selects the onboard waveform library using the specified haptic device.
 */
error_t haptic_select_library(struct Device* device, uint8_t library);

/**
 * @brief Starts playback using the specified haptic device.
 */
error_t haptic_start_playback(struct Device* device);

/**
 * @brief Stops playback using the specified haptic device.
 */
error_t haptic_stop_playback(struct Device* device);

extern const struct DeviceType HAPTIC_TYPE;

#ifdef __cplusplus
}
#endif
