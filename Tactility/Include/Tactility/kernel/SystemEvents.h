#pragma once

#include <cstdint>
#include <functional>

namespace tt::kernel {

enum class SystemEvent {
    BootInitHalBegin,
    BootInitHalEnd,
    BootInitI2cBegin,
    BootInitI2cEnd,
    BootInitSpiBegin,
    BootInitSpiEnd,
    BootInitUartBegin,
    BootInitUartEnd,
    BootSplash,
    /** Gained IP address */
    NetworkConnected,
    NetworkDisconnected,
    /** LVGL devices are initialized and usable */
    LvglStarted,
    /** LVGL devices were removed and not usable anymore */
    LvglStopped,
    /** An important system time-related event, such as NTP update or time-zone change */
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