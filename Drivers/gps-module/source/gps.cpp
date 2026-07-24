// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/gps.h>

#include "init.h"
#include "probe.h"

#include <tactility/check.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <tactility/time.h>

#include <minmea.h>

#include <cstdlib>

constexpr auto* TAG = "Gps";
#define GET_CONFIG(device) (static_cast<const GpsConfig*>((device)->config))

constexpr uint32_t GPS_UART_BUFFER_SIZE = 256;
constexpr TickType_t GPS_THREAD_STOP_TIMEOUT_TICKS = pdMS_TO_TICKS(5000);
constexpr TickType_t GPS_THREAD_STOP_POLL_TICKS = pdMS_TO_TICKS(10);

struct GpsInternal {
    RecursiveMutex mutex;
    Thread* thread;
    volatile bool interrupt_requested;
    GpsState state;
    // Mirrors GpsConfig::model, but overwritten with the autodetected model once probing succeeds.
    GpsModel model;
    bool has_rmc;
    minmea_sentence_rmc rmc;
    TickType_t rmc_time;
    bool has_gga;
    minmea_sentence_gga gga;
    TickType_t gga_time;
};

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
    auto* uart = device_get_parent(device);
    check(device_get_type(uart) == &UART_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    UartConfig uart_config = {
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
            switch (minmea_sentence_id((char*)buffer, false)) {
                case MINMEA_SENTENCE_RMC: {
                    minmea_sentence_rmc rmc_frame;
                    if (minmea_parse_rmc(&rmc_frame, (char*)buffer)) {
                        recursive_mutex_lock(&internal->mutex);
                        internal->has_rmc = true;
                        internal->rmc = rmc_frame;
                        internal->rmc_time = get_ticks();
                        recursive_mutex_unlock(&internal->mutex);
                    } else {
                        LOG_E(TAG, "RMC parse error: %s", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                }
                case MINMEA_SENTENCE_GGA: {
                    minmea_sentence_gga gga_frame;
                    if (minmea_parse_gga(&gga_frame, (char*)buffer)) {
                        recursive_mutex_lock(&internal->mutex);
                        internal->has_gga = true;
                        internal->gga = gga_frame;
                        internal->gga_time = get_ticks();
                        recursive_mutex_unlock(&internal->mutex);
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

    set_state(internal, GpsState::GPS_STATE_OFF);
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
    internal->state = GpsState::GPS_STATE_PENDING_ON;

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
    internal->state = GpsState::GPS_STATE_PENDING_OFF;
    recursive_mutex_unlock(&internal->mutex);

    if (thread_join(internal->thread, GPS_THREAD_STOP_TIMEOUT_TICKS, GPS_THREAD_STOP_POLL_TICKS) != ERROR_NONE) {
        LOG_W(TAG, "GPS thread for %s did not stop in time", device->name);
    }
    thread_free(internal->thread);

    recursive_mutex_destruct(&internal->mutex);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region GpsApi

static error_t gps_api_get_rmc(Device* device, minmea_sentence_rmc* out, TickType_t max_age) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));

    recursive_mutex_lock(&internal->mutex);
    error_t result;
    if (!internal->has_rmc) {
        result = ERROR_NOT_FOUND;
    } else if (get_ticks() - internal->rmc_time > max_age) {
        result = ERROR_TIMEOUT;
    } else {
        *out = internal->rmc;
        result = ERROR_NONE;
    }
    recursive_mutex_unlock(&internal->mutex);
    return result;
}

static error_t gps_api_get_gga(Device* device, minmea_sentence_gga* out, TickType_t max_age) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));

    recursive_mutex_lock(&internal->mutex);
    error_t result;
    if (!internal->has_gga) {
        result = ERROR_NOT_FOUND;
    } else if (get_ticks() - internal->gga_time > max_age) {
        result = ERROR_TIMEOUT;
    } else {
        *out = internal->gga;
        result = ERROR_NONE;
    }
    recursive_mutex_unlock(&internal->mutex);
    return result;
}

static GpsModel gps_api_get_model(Device* device) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    recursive_mutex_lock(&internal->mutex);
    auto model = internal->model;
    recursive_mutex_unlock(&internal->mutex);
    return model;
}

static GpsState gps_api_get_state(Device* device) {
    auto* internal = static_cast<GpsInternal*>(device_get_driver_data(device));
    recursive_mutex_lock(&internal->mutex);
    auto state = internal->state;
    recursive_mutex_unlock(&internal->mutex);
    return state;
}

// endregion

const char* gps_model_to_string(enum GpsModel model) {
    switch (model) {
        case GPS_MODEL_AG3335: return "AG3335";
        case GPS_MODEL_AG3352: return "AG3352";
        case GPS_MODEL_ATGM336H: return "ATGM336H";
        case GPS_MODEL_LS20031: return "LS20031";
        case GPS_MODEL_MTK: return "MTK";
        case GPS_MODEL_MTK_L76B: return "MTK_L76B";
        case GPS_MODEL_MTK_PA1616S: return "MTK_PA1616S";
        case GPS_MODEL_UBLOX6: return "UBLOX6";
        case GPS_MODEL_UBLOX7: return "UBLOX7";
        case GPS_MODEL_UBLOX8: return "UBLOX8";
        case GPS_MODEL_UBLOX9: return "UBLOX9";
        case GPS_MODEL_UBLOX10: return "UBLOX10";
        case GPS_MODEL_UC6580: return "UC6580";
        default: return "Unknown";
    }
}

error_t gps_get_rmc(Device* device, minmea_sentence_rmc* out, TickType_t max_age) {
    const auto* driver = device_get_driver(device);
    return static_cast<const GpsApi*>(driver->api)->get_rmc(device, out, max_age);
}

error_t gps_get_gga(Device* device, minmea_sentence_gga* out, TickType_t max_age) {
    const auto* driver = device_get_driver(device);
    return static_cast<const GpsApi*>(driver->api)->get_gga(device, out, max_age);
}

enum GpsModel gps_get_model(Device* device) {
    const auto* driver = device_get_driver(device);
    return static_cast<const GpsApi*>(driver->api)->get_model(device);
}

enum GpsState gps_get_state(Device* device) {
    const auto* driver = device_get_driver(device);
    return static_cast<const GpsApi*>(driver->api)->get_state(device);
}

const DeviceType GPS_TYPE {
    .name = "gps"
};

static const GpsApi gps_api = {
    .get_rmc = gps_api_get_rmc,
    .get_gga = gps_api_get_gga,
    .get_model = gps_api_get_model,
    .get_state = gps_api_get_state,
};

extern Module gps_module;

Driver gps_driver = {
    .name = "gps",
    .compatible = (const char*[]) { "generic,gps", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &gps_api,
    .device_type = &GPS_TYPE,
    .owner = &gps_module
};
