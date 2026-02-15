#pragma once

#include <memory>
#include <tactility/hal/Device.h>
#include <vector>

namespace tt::hal {

typedef bool (*InitBoot)();

typedef std::vector<std::shared_ptr<Device>> DeviceVector;

typedef std::shared_ptr<Device> (*CreateDevice)();

struct Configuration {
    /**
     * Used for powering on the peripherals manually.
     */
    const InitBoot initBoot = nullptr;

    const std::function<DeviceVector()> createDevices = [] { return DeviceVector(); };
};

} // namespace
