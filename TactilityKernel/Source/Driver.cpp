// SPDX-License-Identifier: Apache-2.0

#include <cstring>
#include <ranges>
#include <vector>

#include <Tactility/concurrent/Mutex.h>
#include <Tactility/Device.h>
#include <Tactility/Driver.h>
#include <Tactility/Error.h>
#include <Tactility/Log.h>

#define TAG LOG_TAG(driver)

struct DriverInternalData {
    Mutex mutex { 0 };
    int use_count = 0;

    DriverInternalData() {
        mutex_construct(&mutex);
    }

    ~DriverInternalData() {
        mutex_destruct(&mutex);
    }
};

struct DriverLedger {
    std::vector<Driver*> drivers;
    Mutex mutex { 0 };

    DriverLedger() {
        mutex_construct(&mutex);
    }

    ~DriverLedger() {
        mutex_destruct(&mutex);
    }

    void lock() {
        mutex_lock(&mutex);
    }

    void unlock() {
        mutex_unlock(&mutex);
    }
};

static DriverLedger& get_ledger() {
    static DriverLedger ledger;
    return ledger;
}

#define ledger get_ledger()

#define driver_internal_data(driver) static_cast<DriverInternalData*>(driver->internal.data)
#define driver_lock(driver) mutex_lock(&driver_internal_data(driver)->mutex);
#define driver_unlock(driver) mutex_unlock(&driver_internal_data(driver)->mutex);

static void driver_add(Driver* driver) {
    LOG_I(TAG, "add %s", driver->name);
    ledger.lock();
    ledger.drivers.push_back(driver);
    ledger.unlock();
}

static bool driver_remove(Driver* driver) {
    LOG_I(TAG, "remove %s", driver->name);

    ledger.lock();
    const auto iterator = std::ranges::find(ledger.drivers, driver);
    // check that there actually is a 3 in our vector
    if (iterator == ledger.drivers.end()) {
        ledger.unlock();
        return false;
    }
    ledger.drivers.erase(iterator);
    ledger.unlock();

    return true;
}

extern "C" {

int driver_construct(Driver* driver) {
    driver->internal.data = new(std::nothrow) DriverInternalData;
    if (driver->internal.data == nullptr) {
        return ENOMEM;
    }
    driver_add(driver);
    return 0;
}

int driver_destruct(Driver* driver) {
    // Check if in use
    if (driver_internal_data(driver)->use_count != 0) {
        return ERROR_INVALID_STATE;
    }

    driver_remove(driver);
    delete driver_internal_data(driver);
    driver->internal.data = nullptr;
    return 0;
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

int driver_bind(Driver* driver, Device* device) {
    driver_lock(driver);

    int err = 0;
    if (!device_is_added(device)) {
        err = ERROR_INVALID_STATE;
        goto error;
    }

    if (driver->start_device != nullptr) {
        err = driver->start_device(device);
        if (err != 0) {
            goto error;
        }
    }

    driver_internal_data(driver)->use_count++;
    driver_unlock(driver);

    LOG_I(TAG, "bound %s to %s", driver->name, device->name);
    return 0;

error:

    driver_unlock(driver);
    return err;
}

int driver_unbind(Driver* driver, Device* device) {
    driver_lock(driver);

    int err = 0;
    if (!device_is_added(device)) {
        err = ERROR_INVALID_STATE;
        goto error;
    }

    if (driver->stop_device != nullptr) {
        err = driver->stop_device(device);
        if (err != 0) {
            goto error;
        }
    }

    driver_internal_data(driver)->use_count--;
    driver_unlock(driver);

    LOG_I(TAG, "unbound %s to %s", driver->name, device->name);

    return 0;

error:

    driver_unlock(driver);
    return err;
}

} // extern "C"
