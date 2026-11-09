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
    /**
     * NMEA talker ID of the sentence, two letters plus NUL: "GP" GPS, "GL" GLONASS, "GA" Galileo,
     * "GB" or "BD" BeiDou, "GQ" QZSS, "GN" combined. Empty when unknown, e.g. for GPS_EVENT_UNSUBSCRIBED.
     */
    char talker[3];
    /**
     * NMEA 4.10 system ID of a GSA sentence: 1 GPS, 2 GLONASS, 3 Galileo, 4 BeiDou, 5 QZSS, 6 NavIC.
     * 0 when the receiver doesn't send it, and for other sentences.
     */
    uint8_t system_id;
};

/** Events that a subscription holds until event_await takes them. Later events are dropped while it's full. */
#define GPS_SUBSCRIPTION_QUEUE_LENGTH 16

struct GpsSubscription {
    TaskHandle_t task;

    /** The event that the last successful event_await took */
    struct GpsEvent event;

    /** Number of events the driver queued */
    uint32_t sequence;
    /** Number of events event_await took */
    uint32_t consumed_sequence;

    struct GpsSubscription* next;

    /** Events not taken yet. Written by the driver. */
    struct GpsEvent queue[GPS_SUBSCRIPTION_QUEUE_LENGTH];
};

/**
 * @brief API for GPS/GNSS receiver drivers.
 */
struct GpsApi {
    /**
     * @brief Registers a subscriber for GPS events (e.g. RMC/GGA sentences).
     *
     * Fails with ERROR_INVALID_STATE until the device started, and once device_stop() started for it. A successful subscription
     * was registered before the DEVICE_EVENT_STOPPING listeners run, and gets GPS_EVENT_UNSUBSCRIBED as
     * its last event when the device stops. The device can be freed after it stopped, so a subscriber
     * either unsubscribes in a DEVICE_EVENT_STOPPING listener, or holds a reference (device_get()) until
     * it unsubscribed or got GPS_EVENT_UNSUBSCRIBED.
     *
     * @param[in] device the GPS device
     * @param[in,out] sub subscription to register; caller owns the storage and must keep it alive until unsubscribed
     * @retval ERROR_NONE on success
     * @retval ERROR_INVALID_STATE if the device isn't started, or is stopping
     */
    error_t (*event_subscribe)(struct Device* device, struct GpsSubscription* sub);

    /**
     * @brief Removes a previously registered subscription.
     * @param[in] device the GPS device
     * @param[in] sub subscription to remove, as passed to event_subscribe
     */
    error_t (*event_unsubscribe)(struct Device* device, struct GpsSubscription* sub);

    /**
     * @brief Takes the oldest queued event into sub->event, and blocks the calling task until one arrives
     * or timeout elapses when none is queued. Only the subscribing task calls this.
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
