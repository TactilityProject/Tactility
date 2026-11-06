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

#include <atomic>
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
    std::atomic<bool> interrupt_requested;
    GpsState state;
    // Mirrors GpsConfig::model, but overwritten with the autodetected model once probing succeeds.
    GpsModel model;
    // Singly-linked list of subscribers, guarded by `mutex`.
    GpsSubscription* subscribers;
};

struct DriverDataMutex {
    RecursiveMutex handle {};
    DriverDataMutex() { recursive_mutex_construct(&handle); }
    ~DriverDataMutex() { recursive_mutex_destruct(&handle); }
};

// Held by the API functions while they use the driver data, so stop() can't free it in the meantime
static DriverDataMutex driver_data_mutex;

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

// Queues `event` for every current subscriber and wakes their waiting task. Safe to call from the
// GPS thread's parsing loop.
static void notify_subscribers(GpsInternal* internal, const GpsEvent& event) {
    recursive_mutex_lock(&internal->mutex);

    for (GpsSubscription* sub = internal->subscribers; sub != nullptr; sub = sub->next) {
        // The subscriber's task advances consumed_sequence without a lock
        const uint32_t consumed = __atomic_load_n(&sub->consumed_sequence, __ATOMIC_ACQUIRE);
        // One slot stays free for GPS_EVENT_UNSUBSCRIBED, the last event
        const uint32_t capacity = event.type == GPS_EVENT_UNSUBSCRIBED ? GPS_SUBSCRIPTION_QUEUE_LENGTH : GPS_SUBSCRIPTION_QUEUE_LENGTH - 1U;
        if (sub->sequence - consumed < capacity) {
            sub->queue[sub->sequence % GPS_SUBSCRIPTION_QUEUE_LENGTH] = event;
            __atomic_store_n(&sub->sequence, sub->sequence + 1U, __ATOMIC_RELEASE);
        }
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

// Returns the optional system ID field of a GSA sentence (NMEA 4.10), or 0 when it's missing
static uint8_t parse_gsa_system_id(const char* sentence) {
    int field = 0;
    for (const char* c = sentence; *c != '\0' && *c != '*'; c++) {
        if (*c == ',' && ++field == 18) {
            const char digit = c[1];
            if (digit >= '1' && digit <= '9') {
                return static_cast<uint8_t>(digit - '0');
            }
            if (digit >= 'A' && digit <= 'F') {
                return static_cast<uint8_t>(digit - 'A' + 10);
            }
            return 0;
        }
    }
    return 0;
}

// region Driver lifecycle

// Returns false when probing or init failed, or when the thread was interrupted
static bool setup_receiver(GpsInternal* internal, Device* uart) {
    GpsModel model = internal->model;
    if (model == GpsModel::GPS_MODEL_UNKNOWN) {
        model = gps_probe(uart, internal->interrupt_requested);
        if (model == GpsModel::GPS_MODEL_UNKNOWN) {
            if (!is_interrupted(internal)) {
                LOG_E(TAG, "Probe failed");
            }
            return false;
        }
        recursive_mutex_lock(&internal->mutex);
        internal->model = model;
        recursive_mutex_unlock(&internal->mutex);
    }

    if (!gps_init(uart, model, internal->interrupt_requested)) {
        if (!is_interrupted(internal)) {
            LOG_E(TAG, "Init failed");
        }
        return false;
    }
    return true;
}

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

    if (setup_receiver(internal, uart)) {
        set_state(internal, GpsState::GPS_STATE_ON);
    } else if (!is_interrupted(internal)) {
        set_state(internal, GpsState::GPS_STATE_ERROR);
        return -1;
    }

    // Reference: https://gpsd.gitlab.io/gpsd/NMEA.html
    uint8_t buffer[GPS_UART_BUFFER_SIZE];
    size_t line_length = 0;
    while (!is_interrupted(internal)) {
        size_t bytes_read = 0;
        const error_t read_result = uart_controller_read_until(uart, buffer + line_length, sizeof(buffer) - line_length, '\n', true, &bytes_read, pdMS_TO_TICKS(100));

        // Thread might've been interrupted in the meanwhile
        if (is_interrupted(internal)) {
            break;
        }

        // A read times out 100 ms after it started, so a line can arrive in several parts
        line_length += bytes_read;
        if (read_result != ERROR_NONE || bytes_read == 0U) {
            if (line_length >= sizeof(buffer) - 1U) {
                line_length = 0;
            }
            continue;
        }
        const bool complete = buffer[line_length - 1U] == '\n';
        line_length = 0;

        if (complete) {
            GpsEvent event {};
            minmea_talker_id(event.talker, reinterpret_cast<char*>(buffer));
            switch (minmea_sentence_id(reinterpret_cast<char*>(buffer), false)) {
                case MINMEA_SENTENCE_RMC: {
                    event.type = GPS_EVENT_MESSAGE_RMC;
                    if (minmea_parse_rmc(&event.data.rmc, reinterpret_cast<char*>(buffer))) {
                        notify_subscribers(internal, event);
                    } else {
                        LOG_E(TAG, "RMC parse error: %s", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                }
                case MINMEA_SENTENCE_GGA: {
                    event.type = GPS_EVENT_MESSAGE_GGA;
                    if (minmea_parse_gga(&event.data.gga, reinterpret_cast<char*>(buffer))) {
                        notify_subscribers(internal, event);
                    } else {
                        LOG_E(TAG, "GGA parse error: %s", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                }
                case MINMEA_SENTENCE_GSV: {
                    event.type = GPS_EVENT_MESSAGE_GSV;
                    if (minmea_parse_gsv(&event.data.gsv, reinterpret_cast<char*>(buffer))) {
                        notify_subscribers(internal, event);
                    } else {
                        LOG_E(TAG, "GSV parse error: %s", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                }
                case MINMEA_SENTENCE_GSA: {
                    event.type = GPS_EVENT_MESSAGE_GSA;
                    if (minmea_parse_gsa(&event.data.gsa, reinterpret_cast<char*>(buffer))) {
                        event.system_id = parse_gsa_system_id(reinterpret_cast<const char*>(buffer));
                        notify_subscribers(internal, event);
                    } else {
                        LOG_E(TAG, "GSA parse error: %s", reinterpret_cast<const char*>(buffer));
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
        recursive_mutex_lock(&driver_data_mutex.handle);
        device_set_driver_data(device, nullptr);
        recursive_mutex_unlock(&driver_data_mutex.handle);
        thread_free(internal->thread);
        recursive_mutex_destruct(&internal->mutex);
        free(internal);
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

    // Also after a failed setup, when the thread ended early. Subscribers that are still registered get
    // the event once, as they're dropped right after it. New subscriptions are refused while interrupt_requested is set.
    notify_subscribers(internal, GpsEvent { .type = GPS_EVENT_UNSUBSCRIBED, .data = {}, .talker = {}, .system_id = 0 });
    recursive_mutex_lock(&internal->mutex);
    internal->subscribers = nullptr;
    recursive_mutex_unlock(&internal->mutex);

    recursive_mutex_lock(&driver_data_mutex.handle);
    device_set_driver_data(device, nullptr);
    recursive_mutex_unlock(&driver_data_mutex.handle);

    thread_free(internal->thread);
    recursive_mutex_destruct(&internal->mutex);
    free(internal);
    return ERROR_NONE;
}

// endregion

// region GpsApi

static error_t gps_api_event_subscribe(Device* device, GpsSubscription* sub) {
    sub->task = xTaskGetCurrentTaskHandle();
    sub->sequence = 0;
    sub->consumed_sequence = 0;

    // device_start() marks the device as started under the device lock after start() succeeded, and
    // device_stop() marks it as stopping under it before it notifies the DEVICE_EVENT_STOPPING listeners.
    // Holding it until the subscriber is registered means that a registered subscriber was added to a
    // started device and always gets that event.
    device_lock(device);
    recursive_mutex_lock(&driver_data_mutex.handle);
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    // Lookups still return the device while it stops, after stop() freed the driver data
    if (internal == nullptr) {
        recursive_mutex_unlock(&driver_data_mutex.handle);
        device_unlock(device);
        return ERROR_INVALID_STATE;
    }

    recursive_mutex_lock(&internal->mutex);
    // A failed start frees the driver data without notifying subscribers. When the device stops, the
    // DEVICE_EVENT_STOPPING listeners already ran, so a new subscriber would never be told that it goes away.
    const bool refused = !device_is_ready(device) || device_is_stopping(device) || internal->interrupt_requested;
    if (!refused) {
        sub->next = internal->subscribers;
        internal->subscribers = sub;
    }
    recursive_mutex_unlock(&internal->mutex);
    recursive_mutex_unlock(&driver_data_mutex.handle);
    device_unlock(device);

    return refused ? ERROR_INVALID_STATE : ERROR_NONE;
}

static error_t gps_api_event_unsubscribe(Device* device, GpsSubscription* sub) {
    recursive_mutex_lock(&driver_data_mutex.handle);
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    if (internal == nullptr) {
        recursive_mutex_unlock(&driver_data_mutex.handle);
        return ERROR_INVALID_STATE;
    }

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
    recursive_mutex_unlock(&driver_data_mutex.handle);

    return result;
}

static error_t gps_api_event_await(Device*, GpsSubscription* sub, TickType_t timeout) {
    // The GPS thread advances sequence after it wrote the queue slot
    while (__atomic_load_n(&sub->sequence, __ATOMIC_ACQUIRE) == sub->consumed_sequence) {
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0) {
            return ERROR_TIMEOUT;
        }
    }

    sub->event = sub->queue[sub->consumed_sequence % GPS_SUBSCRIPTION_QUEUE_LENGTH];
    __atomic_store_n(&sub->consumed_sequence, sub->consumed_sequence + 1U, __ATOMIC_RELEASE);
    return ERROR_NONE;
}

static GpsState gps_api_get_state(Device* device) {
    recursive_mutex_lock(&driver_data_mutex.handle);
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    GpsState state = GPS_STATE_OFF;
    if (internal != nullptr) {
        recursive_mutex_lock(&internal->mutex);
        state = internal->state;
        recursive_mutex_unlock(&internal->mutex);
    }
    recursive_mutex_unlock(&driver_data_mutex.handle);
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
    .internal = nullptr
};
