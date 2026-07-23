#include <Tactility/service/gps/GpsService.h>

#include <Tactility/file/File.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/log.h>

using tt::hal::gps::GpsDevice;

namespace tt::service::gps {

constexpr auto* TAG = "GpsService";
extern const ServiceManifest manifest;

constexpr bool hasTimeElapsed(TickType_t now, TickType_t timeInThePast, TickType_t expireTimeInTicks) {
    return (now - timeInThePast) >= expireTimeInTicks;
}

GpsService::GpsDeviceRecord* GpsService::findGpsRecord(const std::shared_ptr<GpsDevice>& device) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    auto result = std::views::filter(deviceRecords, [&device](auto& record) {
        return record.device.get() == device.get();
    });
    if (!result.empty()) {
        return &result.front();
    } else {
        return nullptr;
    }
}

void GpsService::addGpsDevice(const std::shared_ptr<GpsDevice>& device) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    GpsDeviceRecord record = {.device = device};

    if (getState() == State::On) { // Ignore during OnPending due to risk of data corruption
        startGpsDevice(record);
    }

    deviceRecords.push_back(record);
}

void GpsService::removeGpsDevice(const std::shared_ptr<GpsDevice>& device) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    GpsDeviceRecord* record = findGpsRecord(device);

    if (getState() == State::On) { // Ignore during OnPending due to risk of data corruption
        stopGpsDevice(*record);
    }

    std::erase_if(deviceRecords, [&device](auto& reference) {
        return reference.device.get() == device.get();
    });
}

bool GpsService::onStart(ServiceContext& serviceContext) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    paths = serviceContext.getPaths();
    return true;
}

void GpsService::onStop(ServiceContext& serviceContext) {
    if (getState() == State::On) {
        stopReceiving();
    }
}

bool GpsService::startGpsDevice(GpsDeviceRecord& record) {
    LOG_I(TAG, "[device %u] starting", (unsigned)record.device->getId());

    auto lock = mutex.asScopedLock();
    lock.lock();

    auto device = record.device;

    if (!device->start()) {
        LOG_E(TAG, "[device %u] starting failed", (unsigned)record.device->getId());
        return false;
    }

    record.satelliteSubscriptionId = device->subscribeGga([this](hal::Device::Id deviceId, auto& record) {
        mutex.lock();
        if (record.fix_quality > 0) {
            ggaRecord = record;
            ggaTime = kernel::getTicks();
        }
        onGgaSentence(deviceId, record);
        mutex.unlock();
    });

    record.rmcSubscriptionId = device->subscribeRmc([this](hal::Device::Id deviceId, auto& record) {
        mutex.lock();
        if (record.longitude.value != 0 && record.longitude.scale != 0) {
            rmcRecord = record;
            rmcTime = kernel::getTicks();
        }
        onRmcSentence(deviceId, record);
        mutex.unlock();
    });

    return true;
}

bool GpsService::stopGpsDevice(GpsDeviceRecord& record) {
    LOG_I(TAG, "[device %u] stopping", (unsigned)record.device->getId());

    auto device = record.device;

    device->unsubscribeGga(record.satelliteSubscriptionId);
    device->unsubscribeRmc(record.rmcSubscriptionId);

    record.satelliteSubscriptionId = -1;
    record.rmcSubscriptionId = -1;

    if (!device->stop()) {
        LOG_E(TAG, "[device %u] stopping failed", (unsigned)record.device->getId());
        return false;
    }

    return true;
}

bool GpsService::startReceiving() {
    LOG_I(TAG, "Start receiving");

    if (getState() != State::Off) {
        LOG_E(TAG, "Already receiving");
        return false;
    }

    setState(State::OnPending);

    auto lock = mutex.asScopedLock();
    lock.lock();

    deviceRecords.clear();

    std::vector<hal::gps::GpsConfiguration> configurations;
    if (!getGpsConfigurations(configurations)) {
        LOG_E(TAG, "Failed to get GPS configurations");
        setState(State::Off);
        return false;
    }

    if (configurations.empty()) {
        LOG_E(TAG, "No GPS configurations");
        setState(State::Off);
        return false;
    }

    for (const auto& configuration: configurations) {
        ::Device* uart_device;
        if (device_get_by_name(configuration.uartName, &uart_device) == ERROR_NONE) {
            auto gps_device = std::make_shared<GpsDevice>(
                uart_device,
                configuration.baudRate,
                configuration.model
            );
            addGpsDevice(gps_device);
        } else {
            LOG_E(TAG, "Failed to find device %s", configuration.uartName);
        }
    }

    // Reset times before starting devices to avoid race with incoming data
    rmcTime = 0;
    ggaTime = 0;

    bool started_one_or_more = false;

    for (auto& record: deviceRecords) {
        started_one_or_more |= startGpsDevice(record);
    }

    if (started_one_or_more) {
        setState(State::On);
        return true;
    } else {
        setState(State::Off);
        return false;
    }
}

void GpsService::stopReceiving() {
    LOG_I(TAG, "Stop receiving");

    setState(State::OffPending);

    auto lock = mutex.asScopedLock();
    lock.lock();

    for (auto& record: deviceRecords) {
        stopGpsDevice(record);
    }

    rmcTime = 0;
    ggaTime = 0;

    setState(State::Off);
}

void GpsService::onGgaSentence(hal::Device::Id deviceId, const minmea_sentence_gga& gga) {
    LOG_D(TAG, "[device %u] LAT %f LON %f, satellites: %d", (unsigned)deviceId, minmea_tocoord(&gga.latitude), minmea_tocoord(&gga.longitude), gga.satellites_tracked);
}

void GpsService::onRmcSentence(hal::Device::Id deviceId, const minmea_sentence_rmc& rmc) {
    LOG_D(TAG, "[device %u] LAT %f LON %f, speed: %f", (unsigned)deviceId, minmea_tocoord(&rmc.latitude), minmea_tocoord(&rmc.longitude), minmea_tofloat(&rmc.speed));
}

State GpsService::getState() const {
    auto lock = stateMutex.asScopedLock();
    lock.lock();
    return state;
}

void GpsService::setState(State newState) {
    auto lock = stateMutex.asScopedLock();
    lock.lock();
    state = newState;
    lock.unlock();
    statePubSub->publish(state);
}

bool GpsService::hasCoordinates() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return getState() == State::On && rmcTime != 0 && !hasTimeElapsed(kernel::getTicks(), rmcTime, kernel::secondsToTicks(10));
}

bool GpsService::getCoordinates(minmea_sentence_rmc& rmc) const {
    if (hasCoordinates()) {
        rmc = rmcRecord;
        return true;
    } else {
        return false;
    }
}

bool GpsService::getGga(minmea_sentence_gga& gga) const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    if (getState() == State::On && ggaTime != 0 && !hasTimeElapsed(kernel::getTicks(), ggaTime, kernel::secondsToTicks(10))) {
        gga = ggaRecord;
        return true;
    }
    return false;
}

std::shared_ptr<GpsService> findGpsService() {
    auto service = findServiceById(manifest.id);
    assert(service != nullptr);
    return std::static_pointer_cast<GpsService>(service);
}

extern const ServiceManifest manifest = {
    .id = "Gps",
    .createService = create<GpsService>
};

} // namespace tt::service::gps
