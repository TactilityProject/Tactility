// SPDX-License-Identifier: GPL-3.0
#include <gps/gps.h>
#include <gps_generic/gps_generic.h>
#include <gps_meshtastic/module.h>

#include <gps_generic/private/init.h>
#include <gps_generic/private/probe.h>

#include <tactility/check.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <tactility/time.h>

#include <minmea.h>

#include <cstdio>
#include <cstdlib> // For calloc() in PC builds

constexpr auto* TAG = "gps-meshtastic";

#define GET_CONFIG(device) (static_cast<const GpsConfig*>((device)->config))

constexpr uint32_t GPS_UART_BUFFER_SIZE = 256;
constexpr TickType_t GPS_THREAD_STOP_TIMEOUT_TICKS = pdMS_TO_TICKS(5000);
constexpr TickType_t GPS_THREAD_STOP_POLL_TICKS = pdMS_TO_TICKS(1000);

struct GpsInternal {
    RecursiveMutex mutex;
    Thread* thread;
    volatile bool interrupt_requested;
    GpsState state;
    // Mirrors GpsConfig::model, but overwritten with the autodetected model once probing succeeds.
    GpsModel model;
    // Singly-linked list of subscribers, guarded by `mutex`.
    GpsSubscription* subscribers;
};

static const char* gpsModelToString(GpsModel model) {
    switch (model) {
        case GPS_MODEL_AG3335:
            return "AG3335";
        case GPS_MODEL_AG3352:
            return "AG3352";
        case GPS_MODEL_ATGM336H:
            return "ATGM336H";
        case GPS_MODEL_LS20031:
            return "LS20031";
        case GPS_MODEL_MTK:
            return "MTK";
        case GPS_MODEL_MTK_L76B:
            return "MTK L76B";
        case GPS_MODEL_MTK_PA1616S:
            return "MTK PA1616S";
        case GPS_MODEL_UBLOX6:
            return "U-blox 6";
        case GPS_MODEL_UBLOX7:
            return "U-blox 7";
        case GPS_MODEL_UBLOX8:
            return "U-blox 8";
        case GPS_MODEL_UBLOX9:
            return "U-blox 9";
        case GPS_MODEL_UBLOX10:
            return "U-blox 10";
        case GPS_MODEL_UC6580:
            return "UC6580";
        case GPS_MODEL_UNKNOWN:
            return "Auto-detect";
        default:
            return "Unknown";
    }
}

// Pushes `event` to every current subscriber and wakes their waiting task. Safe to call from the
// GPS thread's parsing loop.
static void notify_subscribers(GpsInternal* internal, const GpsEvent& event) {
    recursive_mutex_lock(&internal->mutex);

    for (GpsSubscription* sub = internal->subscribers; sub != nullptr; sub = sub->next) {
        sub->event = event;
        sub->sequence++;
        xTaskNotifyGive(sub->task);
    }

    recursive_mutex_unlock(&internal->mutex);
}

static void set_state(GpsInternal* internal, GpsState state) {
    recursive_mutex_lock(&internal->mutex);
    internal->state = state;
    recursive_mutex_unlock(&internal->mutex);
}

static bool is_interrupted(GpsInternal* internal) {
    recursive_mutex_lock(&internal->mutex);
    bool result = internal->interrupt_requested;
    recursive_mutex_unlock(&internal->mutex);
    return result;
}

// region Driver lifecycle

static int32_t gps_thread_main(void* context) {
    auto* device = static_cast<Device*>(context);
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* uart = device_get_parent(device);
    check(uart);
    check(device_get_type(uart) == &UART_CONTROLLER_TYPE);

    const UartConfig uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_CONTROLLER_DATA_8_BITS,
        .parity = UART_CONTROLLER_PARITY_DISABLE,
        .stop_bits = UART_CONTROLLER_STOP_BITS_1
    };

    if (uart_controller_set_config(uart, &uart_config) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure UART %s", uart->name);
        set_state(internal, GpsState::GPS_STATE_ERROR);
        return -1;
    }

    if (uart_controller_open(uart) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open UART %s", uart->name);
        set_state(internal, GpsState::GPS_STATE_ERROR);
        return -1;
    }

    GpsModel model = internal->model;
    if (model == GpsModel::GPS_MODEL_UNKNOWN) {
        model = gps_probe(uart);
        if (model == GpsModel::GPS_MODEL_UNKNOWN) {
            LOG_E(TAG, "Probe failed");
            set_state(internal, GpsState::GPS_STATE_ERROR);
            return -1;
        }
        recursive_mutex_lock(&internal->mutex);
        internal->model = model;
        recursive_mutex_unlock(&internal->mutex);
    }

    if (!gps_init(uart, model)) {
        LOG_E(TAG, "Init failed");
        set_state(internal, GpsState::GPS_STATE_ERROR);
        return -1;
    }

    set_state(internal, GpsState::GPS_STATE_ON);

    // Reference: https://gpsd.gitlab.io/gpsd/NMEA.html
    uint8_t buffer[GPS_UART_BUFFER_SIZE];
    while (!is_interrupted(internal)) {
        size_t bytes_read = 0;
        uart_controller_read_until(uart, buffer, sizeof(buffer), '\n', true, &bytes_read, pdMS_TO_TICKS(100));

        // Thread might've been interrupted in the meanwhile
        if (is_interrupted(internal)) {
            break;
        }

        if (bytes_read > 0U) {
            switch (minmea_sentence_id(reinterpret_cast<char*>(buffer), false)) {
                case MINMEA_SENTENCE_RMC: {
                    GpsEvent event { .type = GPS_EVENT_MESSAGE_RMC, .data = {} };
                    if (minmea_parse_rmc(&event.data.rmc, reinterpret_cast<char*>(buffer))) {
                        notify_subscribers(internal, event);
                    } else {
                        LOG_E(TAG, "RMC parse error: %s", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                }
                case MINMEA_SENTENCE_GGA: {
                    GpsEvent event { .type = GPS_EVENT_MESSAGE_GGA, .data = {} };
                    if (minmea_parse_gga(&event.data.gga, reinterpret_cast<char*>(buffer))) {
                        notify_subscribers(internal, event);
                    } else {
                        LOG_E(TAG, "GGA parse error: %s", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    if (uart_controller_close(uart) != ERROR_NONE) {
        LOG_W(TAG, "Failed to close UART %s", uart->name);
    }

    // Wake any subscribers still awaiting an event so they don't block forever on a device that's
    // going away, then drop them - stop() is about to free `internal`.
    notify_subscribers(internal, GpsEvent { .type = GPS_EVENT_UNSUBSCRIBED, .data = {} });
    recursive_mutex_lock(&internal->mutex);
    internal->subscribers = nullptr;
    recursive_mutex_unlock(&internal->mutex);

    set_state(internal, GPS_STATE_OFF);
    return 0;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<GpsInternal*>(calloc(1, sizeof(GpsInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    recursive_mutex_construct(&internal->mutex);
    internal->model = config->model;
    internal->state = GPS_STATE_PENDING_ON;
    internal->thread = thread_alloc_full("gps", 4096, gps_thread_main, device, -1);
    if (internal->thread == nullptr) {
        recursive_mutex_destruct(&internal->mutex);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    thread_set_priority(internal->thread, THREAD_PRIORITY_HIGH);

    device_set_driver_data(device, internal);

    if (thread_start(internal->thread) != ERROR_NONE) {
        thread_free(internal->thread);
        recursive_mutex_destruct(&internal->mutex);
        free(internal);
        device_set_driver_data(device, nullptr);
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));

    recursive_mutex_lock(&internal->mutex);
    internal->interrupt_requested = true;
    internal->state = GPS_STATE_PENDING_OFF;
    recursive_mutex_unlock(&internal->mutex);

    if (thread_join(internal->thread, GPS_THREAD_STOP_TIMEOUT_TICKS, GPS_THREAD_STOP_POLL_TICKS) != ERROR_NONE) {
        LOG_E(TAG, "GPS thread for %s did not stop in time", device->name);
        return ERROR_RESOURCE_BUSY;
    }
    thread_free(internal->thread);

    recursive_mutex_destruct(&internal->mutex);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region GpsApi

static error_t gps_api_event_subscribe(Device* device, GpsSubscription* sub) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));

    sub->task = xTaskGetCurrentTaskHandle();
    sub->sequence = 0;
    sub->consumed_sequence = 0;

    recursive_mutex_lock(&internal->mutex);
    sub->next = internal->subscribers;
    internal->subscribers = sub;
    recursive_mutex_unlock(&internal->mutex);

    return ERROR_NONE;
}

static error_t gps_api_event_unsubscribe(Device* device, GpsSubscription* sub) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));

    error_t result = ERROR_NOT_FOUND;
    recursive_mutex_lock(&internal->mutex);
    for (GpsSubscription** link = &internal->subscribers; *link != nullptr; link = &(*link)->next) {
        if (*link == sub) {
            *link = sub->next;
            result = ERROR_NONE;
            break;
        }
    }
    recursive_mutex_unlock(&internal->mutex);

    return result;
}

static error_t gps_api_event_await(Device*, GpsSubscription* sub, TickType_t timeout) {
    uint32_t old_sequence = sub->sequence;

    while (sub->sequence == old_sequence) {
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0) {
            return ERROR_TIMEOUT;
        }
    }

    sub->consumed_sequence = sub->sequence;
    return ERROR_NONE;
}

static GpsState gps_api_get_state(Device* device) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    recursive_mutex_lock(&internal->mutex);
    auto state = internal->state;
    recursive_mutex_unlock(&internal->mutex);
    return state;
}

static error_t gps_api_get_model_name(Device* device, char* model_name, size_t buffer_size) {
    const auto* config = GET_CONFIG(device);
    const char* name_to_set = gpsModelToString(config->model);
    snprintf(model_name, buffer_size, "%s", name_to_set);
    return ERROR_NONE;
}

// endregion

static const GpsApi generic_gps_api = {
    .event_subscribe = gps_api_event_subscribe,
    .event_unsubscribe = gps_api_event_unsubscribe,
    .event_await = gps_api_event_await,
    .get_state = gps_api_get_state,
    .get_model_name = gps_api_get_model_name
};

extern Module gps_generic_module;

Driver meshtastic_gps_driver = {
    .name = "gps-generic",
    .compatible = (const char*[]) { "tactility,gps-generic", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &generic_gps_api,
    .device_type = &GPS_TYPE,
    .owner = &gps_meshtastic_module,
    .internal = nullptr
};
