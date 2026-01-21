#include <Tactility/Device.h>
#include <Tactility/Error.h>
#include <Tactility/Driver.h>
#include <Tactility/Log.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <sys/errno.h>
#include <vector>

#define TAG LOG_TAG(device)

struct DeviceData {
    std::vector<Device*> children;
};

struct DeviceLedger {
    std::vector<Device*> devices;
    Mutex mutex {};

    DeviceLedger() {
        mutex_construct(&mutex);
    }

    ~DeviceLedger() {
        mutex_destruct(&mutex);
    }
};

static DeviceLedger ledger;

extern "C" {

#define ledger_lock() mutex_lock(&ledger.mutex);
#define ledger_unlock() mutex_unlock(&ledger.mutex);

#define get_device_data(device) static_cast<DeviceData*>(device->internal.data)

void device_construct(Device* device) {
    LOG_I(TAG, "construct %s", device->name);
    device->internal.data = new DeviceData();
    mutex_construct(&device->internal.mutex);
}

int device_destruct(Device* device) {
    LOG_I(TAG, "destruct %s", device->name);
    mutex_destruct(&device->internal.mutex);
    delete get_device_data(device);
    device->internal.data = nullptr;
    return 0;
}

/** Add a child to the list of children */
static void device_add_child(struct Device* device, struct Device* child) {
    device_lock(device);
    get_device_data(device)->children.push_back(device);
    device_unlock(device);
}

/** Remove a child from the list of children */
static void device_remove_child(struct Device* device, struct Device* child) {
    device_lock(device);
    auto* parent_data = get_device_data(device);
    const auto iterator = std::ranges::find(parent_data->children, device);
    if (iterator != parent_data->children.end()) {
        parent_data->children.erase(iterator);
    }
    device_unlock(device);
}

void device_add(Device* device) {
    LOG_I(TAG, "add %s", device->name);

    assert(!device->internal.state.started);

    // Already added
    if (device->internal.state.added) {
        return;
    }

    // Add to ledger
    ledger_lock();
    ledger.devices.push_back(device);
    ledger_unlock();

    // Add self to parent's children list
    auto* parent = device->internal.parent;
    if (parent != nullptr) {
        device_add_child(parent, device);
    }

    auto* bus = device->internal.bus;
    if (bus != nullptr) {
        bus_add_device(bus, device);
    }

    device->internal.state.added = true;
}

bool device_remove(Device* device) {
    LOG_I(TAG, "remove %s", device->name);

    assert(!device->internal.state.started);

    // Already removed
    if (!device->internal.state.added) {
        return true;
    }

    auto* bus = device->internal.bus;
    if (bus != nullptr) {
        bus_remove_device(bus, device);
    }

    // Remove self from parent's children list
    auto* parent = device->internal.parent;
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
    return true;

failed_ledger_lookup:

    // Re-add to parent
    if (parent != nullptr) {
        device_add_child(parent, device);
    }

    // Re-add to bus
    if (bus != nullptr) {
        bus_add_device(bus, device);
    }

    return false;
}

int device_start(Device* device) {
    if (!device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }

    // Already started
    if (device->internal.state.started) {
        return 0;
    }

    if (device->internal.driver == nullptr) {
        LOG_E(TAG, "start error: no driver for %s", device->name);
        return ERROR_INVALID_STATE;
    }

    int result = driver_bind(device->internal.driver, device);
    if (result != 0) {
        device->internal.state.started = true;
        device->internal.state.start_result = result;
    }

    return 0;
}

int device_stop(struct Device* device) {
    if (!device->internal.state.added) {
        return ERROR_INVALID_STATE;
    }

    // Already stopped
    if (!device->internal.state.started) {
        return 0;
    }

    int result = driver_unbind(device->internal.driver, device);
    if (result != 0) {
        // Re-add to bus
        if (device->internal.bus != nullptr) {
            bus_add_device(device->internal.bus, device);
        }
        return result;
    }

    device->internal.state.started = false;
    device->internal.state.start_result = 0;
    return 0;
}

void device_set_parent(Device* device, Device* parent) {
    assert(!device->internal.state.started);
    device->internal.parent = parent;
}

} // extern "C"
