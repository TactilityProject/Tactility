#pragma once

#include <memory>
#include <tactility/hal/Device.h>
#include <vector>

namespace tt::hal {

typedef bool (*InitBoot)();

typedef std::vector<std::shared_ptr<Device>> DeviceVector;

typedef std::shared_ptr<Device> (*CreateDevice)();

/** Affects LVGL widget style */
enum class UiDensity {
    /** Ideal for very small non-touch screen devices (e.g. Waveshare S3 LCD 1.3") */
    Compact,
    /** Nothing was changed in the LVGL UI/UX */
    Default
};

struct Configuration {
    /**
     * Used for powering on the peripherals manually.
     */
    const InitBoot initBoot = nullptr;

    /** Modify LVGL widget size */
    const UiDensity uiDensity = UiDensity::Default;

    const std::function<DeviceVector()> createDevices = [] { return DeviceVector(); };
};

} // namespace
