// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/task.h>

#include <minmea.h>

/**
 * @brief Supported GPS/GNSS receiver chipsets.
 */
enum GpsModel {
    GPS_MODEL_UNKNOWN = 0,
    GPS_MODEL_AG3335,
    GPS_MODEL_AG3352,
    // CASIC, might work with AT6558, Neoway N58 LTE Cat.1, Neoway G2 and Neoway G7A
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

enum GpsEventType {
    GPS_EVENT_UNSUBSCRIBED, // Last event, device wants to destroy itself and unsubscribed the subscriber.
    GPS_EVENT_MESSAGE_RMC,
    GPS_EVENT_MESSAGE_GGA,
    GPS_EVENT_MESSAGE_GSV,
    GPS_EVENT_MESSAGE_GSA,
};

struct GpsEvent {
    enum GpsEventType type;
    union {
        struct minmea_sentence_rmc rmc;
        struct minmea_sentence_gga gga;
        struct minmea_sentence_gsv gsv;
        struct minmea_sentence_gsa gsa;
    } data;
};

struct GpsSubscription {
    TaskHandle_t task;

    struct GpsEvent event;

    uint32_t sequence;
    uint32_t consumed_sequence;

    struct GpsSubscription* next;
};

/**
 * @brief API for GPS/GNSS receiver drivers.
 */
struct GpsApi {
    /**
     * @brief Registers a subscriber for GPS events (e.g. RMC/GGA sentences).
     * @param[in] device the GPS device
     * @param[in,out] sub subscription to register; caller owns the storage and must keep it alive until unsubscribed
     */
    error_t (*event_subscribe)(struct Device* device, struct GpsSubscription* sub);

    /**
     * @brief Removes a previously registered subscription.
     * @param[in] device the GPS device
     * @param[in] sub subscription to remove, as passed to event_subscribe
     */
    error_t (*event_unsubscribe)(struct Device* device, struct GpsSubscription* sub);

    /**
     * @brief Blocks the calling task until a new event arrives for the subscription, or timeout elapses.
     * @param[in] device the GPS device
     * @param[in,out] sub subscription to wait on
     * @param[in] timeout max ticks to wait
     * @return ERROR_NONE if an event arrived, ERROR_TIMEOUT if the timeout elapsed
     */
    error_t (*event_await)(struct Device* device, struct GpsSubscription* sub, TickType_t timeout);

    /**
     * @brief Gets the current lifecycle state.
     * @param[in] device the GPS device
     */
    enum GpsState (*get_state)(struct Device* device);

    /**
     * @brief Gets a human-readable model name for the device, e.g. "UBLOX8".
     * @param[in] device the GPS device
     * @param[out] model_name buffer to receive the NUL-terminated name
     * @param[in] buffer_size size of model_name in bytes
     * @return ERROR_NONE on success
     */
    error_t (*get_model_name)(struct Device* device, char* model_name, size_t buffer_size);
};

/** @copydoc GpsApi::event_subscribe */
error_t gps_event_subscribe(struct Device* device, struct GpsSubscription* sub);

/** @copydoc GpsApi::event_unsubscribe */
error_t gps_event_unsubscribe(struct Device* device, struct GpsSubscription* sub);

/** @copydoc GpsApi::event_await */
error_t gps_event_await(struct Device* device, struct GpsSubscription* sub, TickType_t timeout);

/** @copydoc GpsApi::get_state */
enum GpsState gps_get_state(struct Device* device);

/** @copydoc GpsApi::get_model_name */
error_t gps_get_model_name(struct Device* device, char* model_name, size_t buffer_size);

extern const struct DeviceType GPS_TYPE;

#ifdef __cplusplus
}
#endif
