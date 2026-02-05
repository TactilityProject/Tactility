// SPDX-License-Identifier: Apache-2.0

#include <cstring>
#include <ranges>
#include <vector>

#include <tactility/driver.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/log.h>

#define TAG "driver"

struct DriverInternal {
    Mutex mutex { 0 };
    int use_count = 0;
    bool destroying = false;

    DriverInternal() {
        mutex_construct(&mutex);
    }

    ~DriverInternal() {
        mutex_destruct(&mutex);
    }
};

struct DriverLedger {
    std::vector<Driver*> drivers;
    Mutex mutex { 0 };

    DriverLedger() { mutex_construct(&mutex); }
    ~DriverLedger() { mutex_destruct(&mutex); }

    void lock() { mutex_lock(&mutex); }
    void unlock() { mutex_unlock(&mutex); }
};

static DriverLedger& get_ledger() {
    static DriverLedger ledger;
    return ledger;
}

#define ledger get_ledger()

#define get_driver_private(driver) static_cast<DriverInternal*>(driver->internal)
#define driver_lock(driver) mutex_lock(&get_driver_private(driver)->mutex);
#define driver_unlock(driver) mutex_unlock(&get_driver_private(driver)->mutex);

extern "C" {

error_t driver_construct(Driver* driver) {
    driver->internal = new(std::nothrow) DriverInternal;
    if (driver->internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    return ERROR_NONE;
}

error_t driver_destruct(Driver* driver) {
    driver_lock(driver);
    // No module means the system owns it and it cannot be destroyed
    if (driver->owner == nullptr) {
        driver_unlock(driver);
        return ERROR_NOT_ALLOWED;
    }
    if (get_driver_private(driver)->use_count != 0 || get_driver_private(driver)->destroying) {
        driver_unlock(driver);
        return ERROR_INVALID_STATE;
    }
    get_driver_private(driver)->destroying = true;

    driver_unlock(driver);
    delete get_driver_private(driver);
    driver->internal = nullptr;

    return ERROR_NONE;
}

error_t driver_add(Driver* driver) {
    LOG_I(TAG, "add %s", driver->name);
    ledger.lock();
    ledger.drivers.push_back(driver);
    ledger.unlock();
    return ERROR_NONE;
}

error_t driver_remove(Driver* driver) {
    LOG_I(TAG, "remove %s", driver->name);

    if (driver->owner == nullptr) return ERROR_NOT_ALLOWED;

    ledger.lock();
    const auto iterator = std::ranges::find(ledger.drivers, driver);
    if (iterator == ledger.drivers.end()) {
        ledger.unlock();
        return ERROR_NOT_FOUND;
    }
    ledger.drivers.erase(iterator);
    ledger.unlock();

    return ERROR_NONE;
}

error_t driver_construct_add(struct Driver* driver) {
    if (driver_construct(driver) != ERROR_NONE) return ERROR_RESOURCE;
    if (driver_add(driver) != ERROR_NONE) return ERROR_RESOURCE;
    return ERROR_NONE;
}

error_t driver_remove_destruct(struct Driver* driver) {
    if (driver_remove(driver) != ERROR_NONE) return ERROR_RESOURCE;
    if (driver_destruct(driver) != ERROR_NONE) return ERROR_RESOURCE;
    return ERROR_NONE;
}

bool driver_is_compatible(Driver* driver, const char* compatible) {
    if (compatible == nullptr || driver->compatible == nullptr) {
        return false;
    }
    const char** compatible_iterator = driver->compatible;
    while (*compatible_iterator != nullptr) {
        if (strcmp(*compatible_iterator, compatible) == 0) {
            return true;
        }
        compatible_iterator++;
    }
    return false;
}

Driver* driver_find_compatible(const char* compatible) {
    ledger.lock();
    Driver* result = nullptr;
    for (auto* driver : ledger.drivers) {
        if (driver_is_compatible(driver, compatible)) {
            result = driver;
            break;
        }
    }
    ledger.unlock();
    return result;
}

error_t driver_bind(Driver* driver, Device* device) {
    driver_lock(driver);

    error_t error = ERROR_NONE;
    if (get_driver_private(driver)->destroying || !device_is_added(device)) {
        error = ERROR_INVALID_STATE;
        goto error;
    }

    if (driver->start_device != nullptr) {
        error = driver->start_device(device);
        if (error != ERROR_NONE) {
            goto error;
        }
    }

    get_driver_private(driver)->use_count++;
    driver_unlock(driver);

    LOG_I(TAG, "bound %s to %s", driver->name, device->name);
    return ERROR_NONE;

error:

    driver_unlock(driver);
    return error;
}

error_t driver_unbind(Driver* driver, Device* device) {
    driver_lock(driver);

    error_t error = ERROR_NONE;
    if (get_driver_private(driver)->destroying || !device_is_added(device)) {
        error = ERROR_INVALID_STATE;
        goto error;
    }

    if (driver->stop_device != nullptr) {
        error = driver->stop_device(device);
        if (error != ERROR_NONE) {
            goto error;
        }
    }

    get_driver_private(driver)->use_count--;
    driver_unlock(driver);

    LOG_I(TAG, "unbound %s from %s", driver->name, device->name);

    return ERROR_NONE;

error:

    driver_unlock(driver);
    return error;
}

const struct DeviceType* driver_get_device_type(struct Driver* driver) {
    return driver->device_type;
}

} // extern "C"
