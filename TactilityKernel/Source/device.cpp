// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <ranges>
#include <cassert>
#include <cstring>
#include <sys/errno.h>
#include <vector>

#define TAG LOG_TAG(device)

struct DevicePrivate {
    std::vector<Device*> children;
};

struct DeviceLedger {
    std::vector<Device*> devices;
    Mutex mutex { 0 };

    DeviceLedger() {
        mutex_construct(&mutex);
    }

    ~DeviceLedger() {
        mutex_destruct(&mutex);
    }
};

static DeviceLedger& get_ledger() {
    static DeviceLedger ledger;
    return ledger;
}

#define ledger get_ledger()

extern "C" {

#define ledger_lock() mutex_lock(&ledger.mutex)
#define ledger_unlock() mutex_unlock(&ledger.mutex)

#define get_device_private(device) static_cast<DevicePrivate*>(device->internal.device_private)

error_t device_construct(Device* device) {
    device->internal.device_private = new(std::nothrow) DevicePrivate;
    if (device->internal.device_private == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    LOG_D(TAG, "construct %s", device->name);
    mutex_construct(&device->internal.mutex);
    return ERROR_NONE;
}

error_t device_destruct(Device* device) {
    if (device->internal.state.started || device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }
    if (!get_device_private(device)->children.empty()) {
        return ERROR_INVALID_STATE;
    }
    LOG_D(TAG, "destruct %s", device->name);
    mutex_destruct(&device->internal.mutex);
    delete get_device_private(device);
    device->internal.device_private = nullptr;
    return ERROR_NONE;
}

/** Add a child to the list of children */
static void device_add_child(struct Device* device, struct Device* child) {
    device_lock(device);
    assert(device->internal.state.added);
    get_device_private(device)->children.push_back(child);
    device_unlock(device);
}

/** Remove a child from the list of children */
static void device_remove_child(struct Device* device, struct Device* child) {
    device_lock(device);
    auto* parent_data = get_device_private(device);
    const auto iterator = std::ranges::find(parent_data->children, child);
    if (iterator != parent_data->children.end()) {
        parent_data->children.erase(iterator);
    }
    device_unlock(device);
}

error_t device_add(Device* device) {
    LOG_D(TAG, "add %s", device->name);

    // Already added
    if (device->internal.state.started || device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }

    // Add to ledger
    ledger_lock();
    ledger.devices.push_back(device);
    ledger_unlock();

    // Add self to parent's children list
    auto* parent = device->parent;
    if (parent != nullptr) {
        device_add_child(parent, device);
    }

    device->internal.state.added = true;
    return ERROR_NONE;
}

error_t device_remove(Device* device) {
    LOG_D(TAG, "remove %s", device->name);

    if (device->internal.state.started || !device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }

    // Remove self from parent's children list
    auto* parent = device->parent;
    if (parent != nullptr) {
        device_remove_child(parent, device);
    }

    ledger_lock();
    const auto iterator = std::ranges::find(ledger.devices, device);
    if (iterator == ledger.devices.end()) {
        ledger_unlock();
        goto failed_ledger_lookup;
    }
    ledger.devices.erase(iterator);
    ledger_unlock();

    device->internal.state.added = false;
    return ERROR_NONE;

failed_ledger_lookup:

    // Re-add to parent
    if (parent != nullptr) {
        device_add_child(parent, device);
    }

    return ERROR_NOT_FOUND;
}

error_t device_start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    if (!device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }

    if (device->internal.driver == nullptr) {
        return ERROR_INVALID_STATE;
    }

    // Already started
    if (device->internal.state.started) {
        return ERROR_NONE;
    }

    error_t bind_error = driver_bind(device->internal.driver, device);
    device->internal.state.started = (bind_error == ERROR_NONE);
    device->internal.state.start_result = bind_error;
    return bind_error == ERROR_NONE ? ERROR_NONE : ERROR_RESOURCE;
}

error_t device_stop(struct Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    if (!device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }

    if (!device->internal.state.started) {
        return ERROR_NONE;
    }

    if (driver_unbind(device->internal.driver, device) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    device->internal.state.started = false;
    device->internal.state.start_result = 0;
    return ERROR_NONE;
}

error_t device_construct_add(struct Device* device, const char* compatible) {
    struct Driver* driver = driver_find_compatible(compatible);
    if (driver == nullptr) {
        LOG_E(TAG, "Can't find driver '%s' for device '%s'", compatible, device->name);
        return ERROR_RESOURCE;
    }

    error_t error = device_construct(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct device %s: %s", device->name, error_to_string(error));
        goto on_construct_error;
    }

    device_set_driver(device, driver);

    error = device_add(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add device %s: %s", device->name, error_to_string(error));
        goto on_add_error;
    }

    return ERROR_NONE;

    on_add_error:
    device_destruct(device);
    on_construct_error:
    return error;
}

error_t device_construct_add_start(struct Device* device, const char* compatible) {
    error_t error = device_construct_add(device, compatible);
    if (error != ERROR_NONE) {
        goto on_construct_add_error;
    }

    error = device_start(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start device %s: %s", device->name, error_to_string(error));
        goto on_start_error;
    }

    return ERROR_NONE;

on_start_error:
    device_remove(device);
    device_destruct(device);
on_construct_add_error:
    return error;
}

void device_set_parent(Device* device, Device* parent) {
    assert(!device->internal.state.started);
    device->parent = parent;
}

void for_each_device(void* callback_context, bool(*on_device)(Device* device, void* context)) {
    ledger_lock();
    for (auto* device : ledger.devices) {
        if (!on_device(device, callback_context)) {
            break;
        }
    }
    ledger_unlock();
}

void for_each_device_child(Device* device, void* callbackContext, bool(*on_device)(struct Device* device, void* context)) {
    auto* data = get_device_private(device);
    for (auto* child_device : data->children) {
        if (!on_device(child_device, callbackContext)) {
            break;
        }
    }
}

void for_each_device_of_type(const DeviceType* type, void* callbackContext, bool(*on_device)(Device* device, void* context)) {
    ledger_lock();
    for (auto* device : ledger.devices) {
        auto* driver = device->internal.driver;
        if (driver != nullptr) {
            if (driver->deviceType == type) {
                if (!on_device(device, callbackContext)) {
                    break;
                }
            }
        }
    }
    ledger_unlock();
}

} // extern "C"
