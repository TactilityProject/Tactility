#include <sys/errno.h>
#include <vector>

#include <Tactility/concurrent/Mutex.h>
#include <Tactility/Driver.h>
#include <Tactility/Error.h>
#include <Tactility/Log.h>

#define TAG LOG_TAG(driver)

struct DriverInternalData {
    Mutex mutex {};
    int use_count = 0;

    DriverInternalData() {
        mutex_construct(&mutex);
    }

    ~DriverInternalData() {
        mutex_destruct(&mutex);
    }
};

struct DriverLedger {
    std::vector<Driver*> drivers = {};
    Mutex mutex {};

    DriverLedger() {
        mutex_construct(&mutex);
    }

    ~DriverLedger() {
        mutex_destruct(&mutex);
    }
};

static DriverLedger ledger;

#define ledger_lock() mutex_lock(&ledger.mutex);
#define ledger_unlock() mutex_unlock(&ledger.mutex);

#define driver_internal_data(driver) static_cast<DriverInternalData*>(driver->internal.data)
#define driver_lock(driver) mutex_lock(&driver_internal_data(driver)->mutex);
#define driver_unlock(driver) mutex_unlock(&driver_internal_data(driver)->mutex);

static void driver_add(Driver* dev) {
    LOG_I(TAG, "add %s", dev->name);
    ledger_lock();
    ledger.drivers.push_back(dev);
    ledger_unlock();
}

static bool driver_remove(Driver* dev) {
    LOG_I(TAG, "remove %s", dev->name);

    ledger_lock();
    const auto iterator = std::ranges::find(ledger.drivers, dev);
    // check that there actually is a 3 in our vector
    if (iterator == ledger.drivers.end()) {
        return false;
    }
    ledger.drivers.erase(iterator);
    ledger_unlock();

    return true;
}

extern "C" {

int driver_construct(Driver* driver) {
    driver->internal.data = new DriverInternalData();
    driver_add(driver);
    return 0;
}

int driver_destruct(Driver* driver) {
    // Check if in use
    if (driver_internal_data(driver)->use_count == 0) {
        return ERROR_INVALID_STATE;
    }

    driver_remove(driver);
    delete driver_internal_data(driver);
    return 0;
}

Driver* driver_find(const char* compatible) {
    ledger_lock();
    const auto it = std::ranges::find_if(ledger.drivers, [compatible](Driver* driver) {
        const char** current_compatible = driver->compatible;
        assert(current_compatible != nullptr);
        while (*current_compatible != nullptr) {
            if (strcmp(compatible, *current_compatible) == 0) {
                return true;
            }
            current_compatible++;
        }
        return false;
    });
    auto* driver = (it != ledger.drivers.end()) ? *it : nullptr;
    ledger_unlock();
    return driver;
}

int driver_bind(Driver* driver, Device* device) {
    driver_lock(driver);

    int err = 0;
    if (!device_is_added(device)) {
        err = -ENODEV;
        goto error;
    }

    device_set_driver(device, driver);

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

    if (driver->stop_device == nullptr) {
        return 0;
    }

    int err = driver->stop_device(device);
    if (err != 0) {
        goto error;
    }

    device_set_driver(device, nullptr);
    driver_internal_data(driver)->use_count--;
    driver_unlock(driver);

    LOG_I(TAG, "unbound %s to %s", driver->name, device->name);

    return 0;

error:

    driver_unlock(driver);
    return err;
}

} // extern "C"
