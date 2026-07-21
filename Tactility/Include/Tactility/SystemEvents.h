#pragma once

#include <cstdint>
#include <functional>

namespace tt::kernel {

enum class SystemEvent {
    BootInitHalBegin,
    BootInitHalEnd,
    BootSplash,
    /** Gained IP address */
    NetworkConnected,
    NetworkDisconnected,
    Time,
};

/** Value 0 mean "no subscription" */
typedef uint32_t SystemEventSubscription;
constexpr SystemEventSubscription NoSystemEventSubscription = 0U;

typedef std::function<void(SystemEvent)> OnSystemEvent;

void publishSystemEvent(SystemEvent event);

SystemEventSubscription subscribeSystemEvent(SystemEvent event, OnSystemEvent handler);

void unsubscribeSystemEvent(SystemEventSubscription subscription);

}