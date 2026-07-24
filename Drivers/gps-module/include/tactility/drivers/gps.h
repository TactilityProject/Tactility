// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/freertos/freertos.h>

#include <minmea.h>

/**
 * @brief Supported GPS/GNSS receiver chipsets.
 */
enum GpsModel {
    GPS_MODEL_UNKNOWN = 0,
    GPS_MODEL_AG3335,
    GPS_MODEL_AG3352,
    /** Casic - might work with AT6558, Neoway N58 LTE Cat.1, Neoway G2, Neoway G7A */
    GPS_MODEL_ATGM336H,
    GPS_MODEL_LS20031,
    GPS_MODEL_MTK,
    GPS_MODEL_MTK_L76B,
    GPS_MODEL_MTK_PA1616S,
    GPS_MODEL_UBLOX6,
    GPS_MODEL_UBLOX7,
    GPS_MODEL_UBLOX8,
    GPS_MODEL_UBLOX9,
    GPS_MODEL_UBLOX10,
    GPS_MODEL_UC6580,
};

/** @return a human-readable name for the model, e.g. "UBLOX8" or "Unknown" */
const char* gps_model_to_string(enum GpsModel model);

/**
 * @brief Lifecycle state of a GPS_TYPE device.
 */
enum GpsState {
    GPS_STATE_OFF,
    GPS_STATE_PENDING_ON,
    GPS_STATE_ON,
    GPS_STATE_ERROR,
    GPS_STATE_PENDING_OFF,
};

/**
 * @brief Configuration for a GPS_TYPE device.
 * @warning Set device_set_parent() to the UART_CONTROLLER_TYPE device this receiver is wired to
 * before starting - the driver reads/writes through its parent.
 */
struct GpsConfig {
    uint32_t baud_rate;
    /** GPS_MODEL_UNKNOWN triggers an autoprobe on start(); the detected model is then available via get_model(). */
    enum GpsModel model;
};

/**
 * @brief API for GPS/GNSS receiver drivers.
 */
struct GpsApi {
    /**
     * @brief Gets the most recently parsed RMC (position/velocity/time) sentence.
     * @param[in] device the GPS device
     * @param[out] out the parsed sentence
     * @param[in] max_age the maximum acceptable age of the cached sentence
     * @retval ERROR_NONE when a sentence younger than max_age was copied into out
     * @retval ERROR_NOT_FOUND when no RMC sentence has ever been parsed
     * @retval ERROR_TIMEOUT when the cached sentence is older than max_age
     */
    error_t (*get_rmc)(struct Device* device, struct minmea_sentence_rmc* out, TickType_t max_age);

    /**
     * @brief Gets the most recently parsed GGA (fix data) sentence.
     * @see GpsApi::get_rmc
     */
    error_t (*get_gga)(struct Device* device, struct minmea_sentence_gga* out, TickType_t max_age);

    /**
     * @brief Gets the model in use - the autodetected model when configured with GPS_MODEL_UNKNOWN.
     * @param[in] device the GPS device
     */
    enum GpsModel (*get_model)(struct Device* device);

    /**
     * @brief Gets the current lifecycle state.
     * @param[in] device the GPS device
     */
    enum GpsState (*get_state)(struct Device* device);
};

/** @copydoc GpsApi::get_rmc */
error_t gps_get_rmc(struct Device* device, struct minmea_sentence_rmc* out, TickType_t max_age);

/** @copydoc GpsApi::get_gga */
error_t gps_get_gga(struct Device* device, struct minmea_sentence_gga* out, TickType_t max_age);

/** @copydoc GpsApi::get_model */
enum GpsModel gps_get_model(struct Device* device);

/** @copydoc GpsApi::get_state */
enum GpsState gps_get_state(struct Device* device);

extern const struct DeviceType GPS_TYPE;

#ifdef __cplusplus
}
#endif
