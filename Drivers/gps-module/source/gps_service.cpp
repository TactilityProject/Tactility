// SPDX-License-Identifier: Apache-2.0
#include <tactility/gps_service.h>
#include "gps_service_internal.h"

#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gps.h>
#include <tactility/log.h>
#include <tactility/service/service_instance.h>
#include <tactility/service/service_manager.h>
#include <tactility/service/service_paths.h>
#include <tactility/time.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

constexpr auto* TAG = "GpsService";

// A dynamically-constructed GPS_TYPE device, one per active GpsConfiguration. Device is the first
// member so `reinterpret_cast<GpsDeviceEntry*>(device)` is never needed - callers just keep the
// Device* and, once done with it, `delete` via a GpsDeviceEntry* they already have.
struct GpsDeviceEntry {
    Device device {};
    GpsConfig config {};
};

struct GpsServiceData {
    RecursiveMutex mutex {};
    std::vector<GpsDeviceEntry*> devices;
    GpsServiceState state = GPS_SERVICE_STATE_OFF;
};

static GpsServiceData* get_data() {
    auto* instance = service_manager_find_instance(GPS_SERVICE_ID);
    if (instance == nullptr) {
        return nullptr;
    }
    return static_cast<GpsServiceData*>(service_instance_get_data(instance));
}

static void set_state(GpsServiceData* data, GpsServiceState state) {
    recursive_mutex_lock(&data->mutex);
    data->state = state;
    recursive_mutex_unlock(&data->mutex);
}

// region Configuration persistence

// Recursively creates every missing directory component of `path` (best-effort - mkdir() failures
// other than "already exists" are surfaced later, when the actual config file open fails).
static void ensure_directory_exists(const char* path) {
    char buffer[224];
    std::strncpy(buffer, path, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    for (char* p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buffer, 0777);
            *p = '/';
        }
    }
    mkdir(buffer, 0777);
}

static bool get_configuration_path(char* out_path, size_t out_path_size) {
    return service_paths_get_user_data_path(GPS_SERVICE_ID, "config.bin", out_path, out_path_size) == ERROR_NONE;
}

void gps_service_for_each_configuration(void* context, void (*on_configuration)(const GpsConfiguration* configuration, size_t index, void* context)) {
    char path[224];
    if (!get_configuration_path(path, sizeof(path))) {
        return;
    }

    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return; // No configurations saved yet
    }

    GpsConfiguration configuration;
    size_t index = 0;
    while (fread(&configuration, sizeof(configuration), 1, file) == 1) {
        on_configuration(&configuration, index, context);
        index++;
    }

    fclose(file);
}

static void collect_configuration(const GpsConfiguration* configuration, size_t, void* context) {
    static_cast<std::vector<GpsConfiguration>*>(context)->push_back(*configuration);
}

static void load_configurations(std::vector<GpsConfiguration>& out) {
    gps_service_for_each_configuration(&out, collect_configuration);
}

static error_t write_configurations(const std::vector<GpsConfiguration>& configurations) {
    char directory[224];
    if (service_paths_get_user_data_directory(GPS_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }
    ensure_directory_exists(directory);

    char path[256];
    if (!get_configuration_path(path, sizeof(path))) {
        return ERROR_RESOURCE;
    }

    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s for writing", path);
        return ERROR_RESOURCE;
    }

    bool ok = true;
    for (auto& configuration : configurations) {
        if (fwrite(&configuration, sizeof(configuration), 1, file) != 1) {
            ok = false;
            break;
        }
    }

    fclose(file);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static bool configurations_equal(const GpsConfiguration& a, const GpsConfiguration& b) {
    return strcmp(a.uart_name, b.uart_name) == 0 &&
        a.baud_rate == b.baud_rate &&
        a.model == b.model;
}

error_t gps_service_add_configuration(const GpsConfiguration* configuration) {
    std::vector<GpsConfiguration> configurations;
    load_configurations(configurations);
    configurations.push_back(*configuration);
    return write_configurations(configurations);
}

error_t gps_service_remove_configuration(const GpsConfiguration* configuration) {
    std::vector<GpsConfiguration> configurations;
    load_configurations(configurations);

    size_t original_size = configurations.size();
    std::erase_if(configurations, [configuration](const GpsConfiguration& item) {
        return configurations_equal(item, *configuration);
    });

    if (configurations.size() == original_size) {
        return ERROR_NOT_FOUND;
    }

    return write_configurations(configurations);
}

// endregion

// region Receiving

static bool construct_add_start(Device* device, Device* parent, const char* name, const void* config, const char* compatible) {
    device->address = 0;
    device->name = name;
    device->config = config;
    device->parent = nullptr;
    device->internal = nullptr;

    if (device_construct(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct %s", name);
        return false;
    }

    device_set_parent(device, parent);

    Driver* driver = driver_find_compatible(compatible);
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for %s", compatible);
        device_destruct(device);
        return false;
    }
    device_set_driver(device, driver);

    if (device_add(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to add %s", name);
        device_destruct(device);
        return false;
    }

    if (device_start(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start %s", name);
        device_remove(device);
        device_destruct(device);
        return false;
    }

    return true;
}

error_t gps_service_start_receiving() {
    auto* data = get_data();
    if (data == nullptr) {
        return ERROR_INVALID_STATE;
    }

    recursive_mutex_lock(&data->mutex);
    if (data->state != GpsServiceState::GPS_SERVICE_STATE_OFF) {
        recursive_mutex_unlock(&data->mutex);
        return ERROR_INVALID_STATE;
    }
    data->state = GpsServiceState::GPS_SERVICE_STATE_ON_PENDING;
    recursive_mutex_unlock(&data->mutex);

    std::vector<GpsConfiguration> configurations;
    load_configurations(configurations);

    if (configurations.empty()) {
        LOG_E(TAG, "No GPS configurations");
        set_state(data, GpsServiceState::GPS_SERVICE_STATE_OFF);
        return ERROR_NOT_FOUND;
    }

    static uint32_t next_device_index = 0;
    bool started_one_or_more = false;

    for (auto& configuration : configurations) {
        Device* uart = nullptr;
        if (device_get_by_name(configuration.uart_name, &uart) != ERROR_NONE) {
            LOG_E(TAG, "Failed to find device %s", configuration.uart_name);
            continue;
        }

        auto* entry = new(std::nothrow) GpsDeviceEntry();
        if (entry == nullptr) {
            device_put(uart);
            continue;
        }
        entry->config = GpsConfig { .baud_rate = configuration.baud_rate, .model = configuration.model };

        char name[16];
        snprintf(name, sizeof(name), "gps%u", (unsigned)next_device_index++);

        bool started = construct_add_start(&entry->device, uart, name, &entry->config, "generic,gps");
        device_put(uart);

        if (started) {
            recursive_mutex_lock(&data->mutex);
            data->devices.push_back(entry);
            recursive_mutex_unlock(&data->mutex);
            started_one_or_more = true;
        } else {
            delete entry;
        }
    }

    if (!started_one_or_more) {
        set_state(data, GPS_SERVICE_STATE_OFF);
        return ERROR_RESOURCE;
    }

    set_state(data, GPS_SERVICE_STATE_ON);
    return ERROR_NONE;
}

void gps_service_stop_receiving() {
    auto* data = get_data();
    if (data == nullptr) {
        return;
    }

    recursive_mutex_lock(&data->mutex);
    if (data->state != GPS_SERVICE_STATE_ON) {
        recursive_mutex_unlock(&data->mutex);
        return;
    }
    data->state = GPS_SERVICE_STATE_OFF_PENDING;

    for (auto* entry : data->devices) {
        device_stop(&entry->device);
        device_remove(&entry->device);
        device_destruct(&entry->device);
        delete entry;
    }
    data->devices.clear();

    data->state = GPS_SERVICE_STATE_OFF;
    recursive_mutex_unlock(&data->mutex);
}

void gps_service_for_each_device(void* context, void (*on_device)(Device* device, void* context)) {
    auto* data = get_data();
    if (data == nullptr) {
        return;
    }

    recursive_mutex_lock(&data->mutex);
    for (auto* entry : data->devices) {
        on_device(&entry->device, context);
    }
    recursive_mutex_unlock(&data->mutex);
}

GpsServiceState gps_service_get_state() {
    auto* data = get_data();
    if (data == nullptr) {
        return GPS_SERVICE_STATE_OFF;
    }
    recursive_mutex_lock(&data->mutex);
    auto state = data->state;
    recursive_mutex_unlock(&data->mutex);
    return state;
}

bool gps_service_get_coordinates(minmea_sentence_rmc* out) {
    auto* data = get_data();
    if (data == nullptr) {
        return false;
    }

    recursive_mutex_lock(&data->mutex);
    bool found = false;
    for (auto* entry : data->devices) {
        if (gps_get_rmc(&entry->device, out, seconds_to_ticks(10)) == ERROR_NONE) {
            found = true;
            break;
        }
    }
    recursive_mutex_unlock(&data->mutex);
    return found;
}

bool gps_service_has_coordinates() {
    minmea_sentence_rmc rmc;
    return gps_service_get_coordinates(&rmc);
}

bool gps_service_get_gga(minmea_sentence_gga* out) {
    auto* data = get_data();
    if (data == nullptr) {
        return false;
    }

    recursive_mutex_lock(&data->mutex);
    bool found = false;
    for (auto* entry : data->devices) {
        if (gps_get_gga(&entry->device, out, seconds_to_ticks(10)) == ERROR_NONE) {
            found = true;
            break;
        }
    }
    recursive_mutex_unlock(&data->mutex);
    return found;
}

// endregion

// region ServiceManifest

static void* create_service(const ServiceManifest*) {
    auto* data = new(std::nothrow) GpsServiceData();
    if (data == nullptr) {
        return nullptr;
    }
    recursive_mutex_construct(&data->mutex);
    return data;
}

static void destroy_service(const ServiceManifest*, void* data) {
    auto* service_data = static_cast<GpsServiceData*>(data);
    recursive_mutex_destruct(&service_data->mutex);
    delete service_data;
}

static void on_stop(ServiceInstance*, void* data) {
    auto* service_data = static_cast<GpsServiceData*>(data);
    if (service_data->state != GpsServiceState::GPS_SERVICE_STATE_OFF) {
        gps_service_stop_receiving();
    }
}

static const ServiceManifest gps_service_manifest = {
    .id = GPS_SERVICE_ID,
    .create_service = create_service,
    .destroy_service = destroy_service,
    .on_start = nullptr,
    .on_stop = on_stop,
};

error_t gps_service_register() {
    return service_manager_add(&gps_service_manifest, true);
}

// endregion
