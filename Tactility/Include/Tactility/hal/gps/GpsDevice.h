#pragma once

#include <tactility/hal/Device.h>
#include "GpsConfiguration.h"
#include "Satellites.h"

#include <Tactility/Thread.h>
#include <Tactility/RecursiveMutex.h>

#include <minmea.h>
#include <utility>

namespace tt::hal::gps {

enum class GpsResponse {
    None,
    NotAck,
    FrameErrors,
    Ok,
};

class GpsDevice : public Device {

public:

    typedef int GgaSubscriptionId;
    typedef int RmcSubscriptionId;

    enum class State {
        PendingOn,
        On,
        Error,
        PendingOff,
        Off
    };

private:

    struct GgaSubscription {
        GgaSubscriptionId id;
        std::shared_ptr<std::function<void(Device::Id id, const minmea_sentence_gga&)>> onData;
    };

    struct RmcSubscription {
        RmcSubscriptionId id;
        std::shared_ptr<std::function<void(Device::Id id, const minmea_sentence_rmc&)>> onData;
    };

    const GpsConfiguration configuration;
    RecursiveMutex mutex;
    std::unique_ptr<Thread> _Nullable thread;
    bool threadInterrupted = false;
    std::vector<GgaSubscription> ggaSubscriptions;
    std::vector<RmcSubscription> rmcSubscriptions;
    GgaSubscriptionId lastSatelliteSubscriptionId = 0;
    RmcSubscriptionId lastRmcSubscriptionId = 0;
    GpsModel model = GpsModel::Unknown;
    State state = State::Off;

    int32_t threadMain();

    bool isThreadInterrupted() const;

    void setState(State newState);

public:

    explicit GpsDevice(GpsConfiguration configuration) : configuration(std::move(configuration)) {}

    ~GpsDevice() override = default;

    Type getType() const override { return Type::Gps; }

    std::string getName() const override {
        if (model != GpsModel::Unknown) {
            return toString(model);
        } else {
            return "Unknown GPS";
        }
    }

    std::string getDescription() const override { return ""; }

    bool start();
    bool stop();

    GgaSubscriptionId subscribeGga(const std::function<void(Device::Id deviceId, const minmea_sentence_gga&)>& onData) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        ggaSubscriptions.push_back({
            .id = ++lastSatelliteSubscriptionId,
            .onData = std::make_shared<std::function<void(Device::Id, const minmea_sentence_gga&)>>(onData)
        });
        return lastSatelliteSubscriptionId;
    }

    void unsubscribeGga(GgaSubscriptionId subscriptionId) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        std::erase_if(ggaSubscriptions, [subscriptionId](auto& subscription) { return subscription.id == subscriptionId; });
    }

    RmcSubscriptionId subscribeRmc(const std::function<void(Device::Id deviceId, const minmea_sentence_rmc&)>& onData) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        rmcSubscriptions.push_back({
            .id = ++lastRmcSubscriptionId,
            .onData = std::make_shared<std::function<void(Device::Id, const minmea_sentence_rmc&)>>(onData)
        });
        return lastRmcSubscriptionId;
    }

    void unsubscribeRmc(RmcSubscriptionId subscriptionId) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        std::erase_if(rmcSubscriptions, [subscriptionId](auto& subscription) { return subscription.id == subscriptionId; });
    }

    GpsModel getModel() const;

    State getState() const;
};

}
