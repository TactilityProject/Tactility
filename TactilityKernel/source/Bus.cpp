#include <algorithm>
#include <cstring>
#include <vector>

#include <Tactility/concurrent/Mutex.h>
#include <Tactility/Bus.h>
#include <Tactility/Device.h>
#include <Tactility/Driver.h>

/** Keeps track of all existing buses */
struct BusLedger {
    std::vector<Bus*> buses = {};
    Mutex mutex {};

    BusLedger() {
        mutex_construct(&mutex);
    }

    ~BusLedger() {
        mutex_destruct(&mutex);
    }
};

/* Internal data for a Bus */
struct BusData {
    std::vector<Device*> devices = {};
    std::vector<Driver*> drivers = {};
    Mutex mutex {};

    BusData() {
        mutex_construct(&mutex);
    }

    ~BusData() {
        mutex_destruct(&mutex);
    }
};

static BusLedger ledger;

#define ledger_lock() mutex_lock(&ledger.mutex);
#define ledger_unlock() mutex_unlock(&ledger.mutex);

#define bus_lock(bus_data) mutex_lock(&bus_data->mutex);
#define bus_unlock(bus_data) mutex_unlock(&bus_data->mutex);

#define BUS_INSTANCE_DATA(bus) ((struct BusData*)bus->internal.data)

extern "C" {

// region Bus management

static int bus_add(Bus* bus) {
    ledger_lock();
    ledger.buses.push_back(bus);
    ledger_unlock();
    return 0;
}

static void bus_remove(Bus* bus) {
    ledger_lock();
    const auto iterator = std::ranges::find(ledger.buses, bus);
    if (iterator != ledger.buses.end()) {
        ledger.buses.erase(iterator);
    }
    ledger_unlock();
}

int bus_construct(Bus* bus) {
    bus->internal.data = new BusData();
    bus_add(bus);
    return 0;
}

int bus_destruct(Bus* bus) {
    delete BUS_INSTANCE_DATA(bus);
    bus_remove(bus);
    return 0;
}

// endregion

// region Bus device management

Bus* bus_find(const char* name) {
    ledger_lock();
    const auto it = std::ranges::find_if(ledger.buses, [name](Bus* bus) {
        return strcmp(name, bus->name) == 0;
    });
    Bus* result = (it != ledger.buses.end()) ? *it : nullptr;
    ledger_unlock();
    return result;
}

int bus_add_device(Bus* bus, Device* dev) {
    auto* bus_data = BUS_INSTANCE_DATA(bus);
    bus_lock(bus_data);
    bus_data->devices.push_back(dev);
    bus_unlock(bus_data);
    return 0;
}

void bus_remove_device(Bus* bus, Device* dev) {
    auto* bus_data = BUS_INSTANCE_DATA(bus);
    bus_lock(bus_data);
    const auto iterator = std::ranges::find(bus_data->devices, dev);
    if (iterator != bus_data->devices.end()) {
        bus_data->devices.erase(iterator);
    }
    bus_unlock(bus_data);
}

// endregion

} // extern "C"
