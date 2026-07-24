// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gps.h>
#include <tactility/error.h>

#include <minmea.h>

struct Module;

#define GPS_SERVICE_ID "gps"

/**
 * @brief Aggregate receive state of the GPS service (across all configured receivers).
 */
enum GpsServiceState {
    GPS_SERVICE_STATE_ON_PENDING,
    GPS_SERVICE_STATE_ON,
    GPS_SERVICE_STATE_OFF_PENDING,
    GPS_SERVICE_STATE_OFF,
};

/**
 * @brief A persisted GPS receiver configuration.
 */
struct GpsConfiguration {
    /** UART controller device name, e.g. "uart0" - resolved via device_get_by_name(). */
    char uart_name[32];
    uint32_t baud_rate;
    /** GPS_MODEL_UNKNOWN triggers an autoprobe. */
    enum GpsModel model;
};

/**
 * @brief Persists a new GPS configuration.
 * @retval ERROR_RESOURCE if the configuration file could not be opened/written
 */
error_t gps_service_add_configuration(const struct GpsConfiguration* configuration);

/**
 * @brief Removes a persisted GPS configuration that matches by value.
 * @retval ERROR_NOT_FOUND if no matching configuration was found
 * @retval ERROR_RESOURCE if the configuration file could not be read/written
 */
error_t gps_service_remove_configuration(const struct GpsConfiguration* configuration);

/**
 * @brief Iterates over all persisted GPS configurations.
 * @param[in] context passed through to on_configuration, can be NULL
 * @param[in] on_configuration called once per configuration, in file order, with its index
 */
void gps_service_for_each_configuration(void* context, void (*on_configuration)(const struct GpsConfiguration* configuration, size_t index, void* context));

/**
 * @brief Iterates over the GPS_TYPE devices currently constructed by gps_service_start_receiving().
 */
void gps_service_for_each_device(void* context, void (*on_device)(struct Device* device, void* context));

/**
 * @brief Constructs and starts a GPS_TYPE device for every persisted configuration and begins receiving.
 * @retval ERROR_INVALID_STATE if already receiving
 * @retval ERROR_NOT_FOUND if there are no persisted configurations, or none of their UART devices could be found
 */
error_t gps_service_start_receiving(void);

/** Stops and destroys every GPS_TYPE device constructed by gps_service_start_receiving(). */
void gps_service_stop_receiving(void);

enum GpsServiceState gps_service_get_state(void);

/** @return true when a coordinate fix is available and is not older than 10 seconds */
bool gps_service_has_coordinates(void);

/** @copydoc gps_service_has_coordinates */
bool gps_service_get_coordinates(struct minmea_sentence_rmc* out);

/** @return true when GGA fix data is available and is not older than 10 seconds */
bool gps_service_get_gga(struct minmea_sentence_gga* out);

extern struct Module gps_module;

#ifdef __cplusplus
}
#endif
