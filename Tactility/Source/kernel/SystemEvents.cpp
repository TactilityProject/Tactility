#include <Tactility/kernel/SystemEvents.h>

#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <tactility/check.h>

#include <Tactility/CoreDefines.h>
#include <list>

namespace tt::kernel {

static const auto LOGGER = Logger("SystemEvents");

struct SubscriptionData {
    SystemEventSubscription id;
    SystemEvent event;
    OnSystemEvent handler;
};

static Mutex mutex;
static SystemEventSubscription subscriptionCounter = 0;
static std::list<SubscriptionData> subscriptions;

static const char* getEventName(SystemEvent event) {
    switch (event) {
        using enum SystemEvent;
        case BootInitHalBegin:
            return TT_STRINGIFY(BootInitHalBegin);
        case BootInitHalEnd:
            return TT_STRINGIFY(BootInitHalEnd);
        case BootInitI2cBegin:
            return TT_STRINGIFY(BootInitI2cBegin);
        case BootInitI2cEnd:
            return TT_STRINGIFY(BootInitI2cEnd);
        case BootInitSpiBegin:
            return TT_STRINGIFY(BootInitSpiBegin);
        case BootInitSpiEnd:
            return TT_STRINGIFY(BootInitSpiEnd);
        case BootInitUartBegin:
            return TT_STRINGIFY(BootInitUartBegin);
        case BootInitUartEnd:
            return TT_STRINGIFY(BootInitUartEnd);
        case BootSplash:
            return TT_STRINGIFY(BootSplash);
        case NetworkConnected:
            return TT_STRINGIFY(NetworkConnected);
        case NetworkDisconnected:
            return TT_STRINGIFY(NetworkDisconnected);
        case LvglStarted:
            return TT_STRINGIFY(LvglStarted);
        case LvglStopped:
            return TT_STRINGIFY(LvglStopped);
        case Time:
            return TT_STRINGIFY(Time);
    }

    check(false); // Missing case above
}

void publishSystemEvent(SystemEvent event) {
    LOGGER.info("{}", getEventName(event));

    if (mutex.lock(MAX_TICKS)) {
        for (auto& subscription : subscriptions) {
            if (subscription.event == event) {
                subscription.handler(event);
            }
        }

        mutex.unlock();
    }
}

SystemEventSubscription subscribeSystemEvent(SystemEvent event, OnSystemEvent handler) {
    if (mutex.lock(MAX_TICKS)) {
        auto id = ++subscriptionCounter;

        subscriptions.push_back({
            .id = id,
            .event = event,
            .handler = handler
        });

        mutex.unlock();
        return id;
    } else {
        check(false);
    }
}

void unsubscribeSystemEvent(SystemEventSubscription subscription) {
    if (mutex.lock(MAX_TICKS)) {
        std::erase_if(subscriptions, [subscription](auto& item) {
            return (item.id == subscription);
        });
        mutex.unlock();
    }
}

}
