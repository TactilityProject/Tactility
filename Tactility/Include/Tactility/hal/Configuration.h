#pragma once

#include <Tactility/hal/sdcard/SdCardDevice.h>

namespace tt::hal {

typedef bool (*InitBoot)();

typedef std::vector<std::shared_ptr<Device>> DeviceVector;

typedef std::shared_ptr<Device> (*CreateDevice)();

/** Affects LVGL widget style */
enum class UiScale {
    /** Ideal for very small non-touch screen devices (e.g. Waveshare S3 LCD 1.3") */
    Smallest,
    /** Nothing was changed in the LVGL UI/UX */
    Default
};

struct Configuration {
    /**
     * Called before I2C/SPI/etc is initialized.
     * Used for powering on the peripherals manually.
     */
    const InitBoot initBoot = nullptr;

    /** Modify LVGL widget size */
    const UiScale uiScale = UiScale::Default;

    std::function<DeviceVector()> createDevices = [] { return DeviceVector(); };
};

} // namespace
