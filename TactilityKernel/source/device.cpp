// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/device.h>
#include <tactility/device_listener_internal.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/check.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/timer.h>

#include <ranges>
#include <cassert>
#include <cstring>
#include <vector>

#define TAG "device"

struct DeviceInternal {
    /** Address of the API exposed by the device instance. */
    Driver* driver = nullptr;
    /** The driver data for this device (e.g. a mutex) */
    void* driver_data = nullptr;
    // The mutex for device operations. Recursive: some drivers (e.g. battery_sense.cpp)
    // legitimately call device_add()/device_start() on a CHILD device from within their own
    // start_device - device_add_child() then locks the PARENT (this same device) again on the
    // same thread. A plain Mutex would deadlock here.
    // NOTE: device_start()/device_stop() do NOT hold this across the driver's
    // start_device/stop_device callback (see state.starting/state.stopping below) - that callback
    // can add/remove child devices, which takes ledger_lock, and lookup helpers
    // (device_get_by_name() etc.) take ledger_lock then this mutex via device_get(). Holding both
    // at once in opposite orders would ABBA-deadlock against those lookups.
    struct RecursiveMutex mutex {};
    /** The device state */
    struct {
        int start_result = 0;
        bool started : 1 = false;
        bool added : 1 = false;
        // Set while driver_bind()/driver_unbind() runs, with `mutex` released. Blocks concurrent
        // device_start()/device_stop() calls from racing the same transition; state.stopping is
        // also checked by device_get() so no new ref can be acquired while a stop is in flight.
        bool starting : 1 = false;
        bool stopping : 1 = false;
    } state;
    /** Attached child devices */
    std::vector<Device*> children {};
    // device_hotplug_poll_once() debounce state.
    struct {
        uint8_t consecutive = 0; // probe() results differing from `present` so far
        bool present = false;
    } hotplug;
    // Outstanding device_get() holders. Guarded by `mutex`. Independent of state.started -
    // device_get()/device_put() bracket construct/destruct, not start/stop, so a ref can be held
    // across a device_stop(). device_destruct() refuses to run while this is > 0.
    int32_t ref_count = 0;
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

#define lock_internal(internal) recursive_mutex_lock(&internal->mutex)
#define unlock_internal(internal) recursive_mutex_unlock(&internal->mutex)

error_t device_construct(Device* device) {
    device->internal = new(std::nothrow) DeviceInternal;
    if (device->internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    LOG_D(TAG, "construct %s", device->name);
    recursive_mutex_construct(&device->internal->mutex);
    return ERROR_NONE;
}

error_t device_destruct(Device* device) {
    lock_internal(device->internal);

    auto* internal = device->internal;

    if (internal->ref_count > 0) {
        unlock_internal(device->internal);
        return ERROR_RESOURCE_BUSY;
    }
    if (internal->state.started || internal->state.added) {
        unlock_internal(device->internal);
        return ERROR_INVALID_STATE;
    }
    if (!internal->children.empty()) {
        unlock_internal(device->internal);
        return ERROR_INVALID_STATE;
    }
    LOG_D(TAG, "destruct %s", device->name);

    device->internal = nullptr;
    recursive_mutex_unlock(&internal->mutex);
    delete internal;

    return ERROR_NONE;
}

/** Add a child to the list of children */
static void device_add_child(Device* device, Device* child) {
    device_lock(device);
    check(device->internal->state.added);
    device->internal->children.push_back(child);
    device_unlock(device);
}

/** Remove a child from the list of children */
static void device_remove_child(Device* device, Device* child) {
    device_lock(device);
    const auto iterator = std::ranges::find(device->internal->children, child);
    if (iterator != device->internal->children.end()) {
        device->internal->children.erase(iterator);
    }
    device_unlock(device);
}

error_t device_add(Device* device) {
    LOG_D(TAG, "add %s", device->name);

    // Already added
    if (device->internal->state.started || device->internal->state.added) {
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

    device->internal->state.added = true;
    return ERROR_NONE;
}

error_t device_remove(Device* device) {
    LOG_D(TAG, "remove %s", device->name);

    if (device->internal->state.started || !device->internal->state.added) {
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

    device->internal->state.added = false;
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
    auto* internal = device->internal;
    lock_internal(internal);

    if (!internal->state.added || internal->driver == nullptr) {
        unlock_internal(internal);
        return ERROR_INVALID_STATE;
    }

    // Already started
    if (internal->state.started) {
        unlock_internal(internal);
        return ERROR_NONE;
    }

    // Already starting on another thread
    if (internal->state.starting) {
        unlock_internal(internal);
        return ERROR_RESOURCE_BUSY;
    }
    internal->state.starting = true;
    unlock_internal(internal);

    // driver_bind() runs the driver's start_device callback, which may add/start child devices
    // (device_add() takes ledger_lock) - `mutex` must stay released across this call. See the
    // comment on `mutex` above.
    error_t bind_error = driver_bind(internal->driver, device);

    lock_internal(internal);
    internal->state.starting = false;
    internal->state.started = (bind_error == ERROR_NONE);
    internal->state.start_result = bind_error;
    unlock_internal(internal);

    if (bind_error != ERROR_NONE) {
        return bind_error == ERROR_NOT_FOUND ? ERROR_NOT_FOUND : ERROR_RESOURCE;
    }

    device_listener_notify(device, DEVICE_EVENT_STARTED);

    return ERROR_NONE;
}

error_t device_stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* internal = device->internal;
    lock_internal(internal);

    if (!internal->state.added) {
        unlock_internal(internal);
        return ERROR_INVALID_STATE;
    }

    if (!internal->state.started) {
        unlock_internal(internal);
        return ERROR_NONE;
    }

    // Already stopping on another thread
    if (internal->state.stopping) {
        unlock_internal(internal);
        return ERROR_RESOURCE_BUSY;
    }
    internal->state.stopping = true;
    unlock_internal(internal);

    device_listener_notify(device, DEVICE_EVENT_STOPPING);

    error_t unbind_error = driver_unbind(internal->driver, device);

    lock_internal(internal);
    internal->state.stopping = false;
    if (unbind_error != ERROR_NONE) {
        unlock_internal(internal);
        return ERROR_RESOURCE;
    }
    internal->state.started = false;
    internal->state.start_result = 0;
    unlock_internal(internal);

    device_listener_notify(device, DEVICE_EVENT_STOPPED);

    return ERROR_NONE;
}

error_t device_construct_add(Device* device, const char* compatible) {
    Driver* driver = driver_find_compatible(compatible);
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

error_t device_construct_add_start(Device* device, const char* compatible) {
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
    assert(!device->internal->state.started);
    device->parent = parent;
}

Device* device_get_parent(Device* device) {
    return device->parent;
}

void device_set_driver(Device* device, Driver* driver) {
    device->internal->driver = driver;
}

Driver* device_get_driver(Device* device) {
    return device->internal->driver;
}

bool device_is_ready(const Device* device) {
    return device->internal->state.started;
}

bool device_is_compatible(const Device* device, const char* compatible) {
    if (device->internal->driver == nullptr) return false;
    return driver_is_compatible(device->internal->driver, compatible);
}

void device_set_driver_data(Device* device, void* driver_data) {
    device->internal->driver_data = driver_data;
}

void* device_get_driver_data(Device* device) {
    return device->internal->driver_data;
}

bool device_is_added(const Device* device) {
    return device->internal->state.added;
}

void device_lock(Device* device) {
    recursive_mutex_lock(&device->internal->mutex);
}

bool device_try_lock(Device* device, TickType_t timeout) {
    return recursive_mutex_try_lock(&device->internal->mutex, timeout);
}

void device_unlock(Device* device) {
    recursive_mutex_unlock(&device->internal->mutex);
}

bool device_is_constructed(const Device* device) {
    return device->internal != nullptr;
}

error_t device_get(Device* device) {
    auto* internal = device->internal;
    if (!internal) {
        return ERROR_INVALID_STATE;
    }
    lock_internal(internal);
    internal->ref_count++;
    unlock_internal(internal);
    return ERROR_NONE;
}

void device_put(Device* device) {
    auto* internal = device->internal;
    lock_internal(internal);
    check(internal->ref_count > 0);
    internal->ref_count--;
    unlock_internal(internal);
}

const DeviceType* device_get_type(Device* device) {
    return device->internal->driver ? device->internal->driver->device_type : NULL;
}

void device_for_each(void* callback_context, bool(*on_device)(Device* device, void* context)) {
    ledger_lock();
    for (auto* device : ledger.devices) {
        if (!on_device(device, callback_context)) {
            break;
        }
    }
    ledger_unlock();
}

void device_for_each_child(Device* device, void* callbackContext, bool(*on_device)(Device* device, void* context)) {
    for (auto* child_device : device->internal->children) {
        if (!on_device(child_device, callbackContext)) {
            break;
        }
    }
}

size_t device_get_child_count(Device* device) {
    return device->internal->children.size();
}

void device_for_each_of_type(const DeviceType* type, void* callbackContext, bool(*on_device)(Device* device, void* context)) {
    ledger_lock();
    for (auto* device : ledger.devices) {
        auto* driver = device->internal->driver;
        if (driver != nullptr) {
            if (driver->device_type == type) {
                if (!on_device(device, callbackContext)) {
                    break;
                }
            }
        }
    }
    ledger_unlock();
}

bool device_exists_of_type(const DeviceType* type) {
    bool found = false;
    ledger_lock();
    for (auto* device : ledger.devices) {
        auto* driver = device->internal->driver;
        if (driver != nullptr && driver->device_type == type) {
            found = true;
            break;
        }
    }
    ledger_unlock();
    return found;
}

error_t device_get_by_name(const char* name, Device** out_device) {
    ledger_lock();
    Device* found = nullptr;
    for (auto* device : ledger.devices) {
        if (device->name != nullptr && std::strcmp(device->name, name) == 0) {
            found = device;
            break;
        }
    }
    if (found == nullptr) {
        ledger_unlock();
        return ERROR_NOT_FOUND;
    }
    // device_get() takes internal->mutex while still holding ledger_lock here on purpose: it
    // blocks device_remove() (which needs ledger_lock to erase `found`) for the span between
    // finding the device and taking a ref on it, so `found` can't be torn down and freed out from
    // under us in that window. This is the reverse lock order from device_start()/device_stop(),
    // which release internal->mutex before touching ledger_lock - see the comment on
    // DeviceInternal::mutex for why that asymmetry is deadlock-free.
    error_t error = device_get(found);
    ledger_unlock();
    if (error == ERROR_NONE) {
        *out_device = found;
    }
    return error;
}

error_t device_get_first_by_type(const DeviceType* type, Device** out_device) {
    ledger_lock();
    Device* found = nullptr;
    for (auto* device : ledger.devices) {
        auto* driver = device->internal->driver;
        if (driver != nullptr && driver->device_type == type) {
            found = device;
            break;
        }
    }
    if (found == nullptr) {
        ledger_unlock();
        return ERROR_NOT_FOUND;
    }
    error_t error = device_get(found);
    ledger_unlock();
    if (error == ERROR_NONE) {
        *out_device = found;
    }
    return error;
}

error_t device_get_first_active_by_type(const DeviceType* type, Device** out_device) {
    ledger_lock();
    Device* found = nullptr;
    for (auto* device : ledger.devices) {
        auto* driver = device->internal->driver;
        if (driver != nullptr && driver->device_type == type && device->internal->state.started) {
            found = device;
            break;
        }
    }
    if (found == nullptr) {
        ledger_unlock();
        return ERROR_NOT_FOUND;
    }
    error_t error = device_get(found);
    ledger_unlock();
    if (error == ERROR_NONE) {
        *out_device = found;
    }
    return error;
}

bool device_has_active_by_type(const struct DeviceType* type) {
    ledger_lock();
    bool found = false;
    for (auto* device : ledger.devices) {
        auto* driver = device->internal->driver;
        if (driver != nullptr && driver->device_type == type && device->internal->state.started) {
            found = true;
            break;
        }
    }
    ledger_unlock();
    return found;
}

error_t device_get_first_by_compatible(const char* compatible, Device** out_device) {
    ledger_lock();
    Device* found = nullptr;
    for (auto* device : ledger.devices) {
        if (device_is_compatible(device, compatible)) {
            found = device;
            break;
        }
    }
    if (found == nullptr) {
        ledger_unlock();
        return ERROR_NOT_FOUND;
    }
    error_t error = device_get(found);
    ledger_unlock();
    if (error == ERROR_NONE) {
        *out_device = found;
    }
    return error;
}

struct HotplugAction {
    Device* device;
    bool start;     // vs. stop
    bool mandatory; // probe == nullptr; a start failure here is a real error
};

// Guards hotplug_registered_devices only - never held during probe()/device_start()/device_stop().
struct HotplugRegistryMutex {
    Mutex handle {};
    HotplugRegistryMutex() { mutex_construct(&handle); }
    ~HotplugRegistryMutex() { mutex_destruct(&handle); }
};

static HotplugRegistryMutex hotplug_registry_mutex;
static std::vector<Device*> hotplug_registered_devices;

static Timer* hotplug_timer = nullptr;

static void hotplug_timer_callback(void*) {
    device_hotplug_poll_once();
}

void device_hotplug_register(Device* device) {
    mutex_lock(&hotplug_registry_mutex.handle);
    if (std::ranges::find(hotplug_registered_devices, device) == hotplug_registered_devices.end()) {
        if (device->internal->driver != nullptr && device->internal->driver->probe != nullptr) {
            device->flags |= DEVICE_FLAG_HOTPLUG;
        }
        hotplug_registered_devices.push_back(device);
    }
    mutex_unlock(&hotplug_registry_mutex.handle);
}

void device_hotplug_unregister(Device* device) {
    mutex_lock(&hotplug_registry_mutex.handle);
    const auto iterator = std::ranges::find(hotplug_registered_devices, device);
    if (iterator != hotplug_registered_devices.end()) {
        hotplug_registered_devices.erase(iterator);
        device->flags &= ~DEVICE_FLAG_HOTPLUG;
    }
    mutex_unlock(&hotplug_registry_mutex.handle);
}

error_t device_hotplug_poll_once(void) {
    // Pin each device (device_get()) so a concurrent unregister+destruct can't free it mid-loop -
    // device_destruct() refuses to run while a reference is outstanding.
    std::vector<Device*> devices;
    mutex_lock(&hotplug_registry_mutex.handle);
    for (Device* device : hotplug_registered_devices) {
        if (device_get(device) == ERROR_NONE) {
            devices.push_back(device);
        }
    }
    mutex_unlock(&hotplug_registry_mutex.handle);

    // Collect actions first: start_device()/stop_device() may add/remove child devices
    // (ledger_lock), so don't call them mid-iteration. Device state is read/written under its own
    // mutex, released before calling probe() - never run caller code under a lock.
    std::vector<HotplugAction> actions;
    for (Device* device : devices) {
        auto* internal = device->internal;

        lock_internal(internal);
        auto* driver = internal->driver;
        bool started = internal->state.started;
        unlock_internal(internal);

        if (driver == nullptr) {
            continue;
        }

        if (driver->probe == nullptr) {
            if (!started) {
                actions.push_back({ device, true, true });
            }
            continue;
        }

        bool present = driver->probe(device) == ERROR_NONE;

        lock_internal(internal);
        bool act = false;
        if (present == internal->hotplug.present) {
            internal->hotplug.consecutive = 0;
        } else if (++internal->hotplug.consecutive >= DEVICE_HOTPLUG_DEBOUNCE_COUNT) {
            // hotplug.present isn't committed yet - only once the action below actually
            // succeeds. Otherwise a failed device_start()/device_stop() would leave it matching
            // the next stable probe(), and the poller would never retry.
            internal->hotplug.consecutive = 0;
            act = true;
        }
        unlock_internal(internal);

        if (act) {
            actions.push_back({ device, present, false });
        }
    }

    error_t first_mandatory_error = ERROR_NONE;
    for (const auto& action : actions) {
        error_t error = action.start ? device_start(action.device) : device_stop(action.device);
        if (error == ERROR_NONE && !action.mandatory) {
            auto* internal = action.device->internal;
            lock_internal(internal);
            internal->hotplug.present = action.start;
            unlock_internal(internal);
        }
        if (action.mandatory && action.start && error != ERROR_NONE && first_mandatory_error == ERROR_NONE) {
            first_mandatory_error = error;
        }
    }

    for (Device* device : devices) {
        device_put(device);
    }

    return first_mandatory_error;
}

void device_hotplug_start_polling(void) {
    if (hotplug_timer != nullptr) {
        return;
    }
    hotplug_timer = timer_alloc(TIMER_TYPE_PERIODIC, pdMS_TO_TICKS(500), hotplug_timer_callback, nullptr);
    timer_start(hotplug_timer);
}

} // extern "C"
