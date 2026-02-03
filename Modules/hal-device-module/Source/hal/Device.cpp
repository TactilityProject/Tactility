// SPDX-License-Identifier: Apache-2.0
#include <tactility/hal/Device.h>
#include <tactility/driver.h>
#include <tactility/drivers/hal_device.hpp>

#include <Tactility/Logger.h>
#include <Tactility/RecursiveMutex.h>
#include <algorithm>

namespace tt::hal {

RecursiveMutex mutex;
static Device::Id nextId = 0;

static const auto LOGGER = Logger("Devices");

Device::Device() : id(nextId++) {}

static std::shared_ptr<Device::KernelDeviceHolder> createKernelDeviceHolder(const std::shared_ptr<Device>& device) {
    auto kernel_device_name = std::format("hal-device-{}", device->getId());
    LOGGER.info("Registering {} with id {} as kernel device {}", device->getName(), device->getId(), kernel_device_name);
    auto kernel_device_holder = std::make_shared<Device::KernelDeviceHolder>(kernel_device_name);
    auto* kernel_device = kernel_device_holder->device.get();
    check(device_construct(kernel_device) == ERROR_NONE);
    check(device_add(kernel_device) == ERROR_NONE);
    auto* driver = driver_find_compatible("hal-device");
    check(driver);
    device_set_driver(kernel_device, driver);
    check(device_start(kernel_device) == ERROR_NONE);
    hal_device_set_device(kernel_device, device);
    return kernel_device_holder;
}

static void destroyKernelDeviceHolder(std::shared_ptr<Device::KernelDeviceHolder>& holder) {
    auto kernel_device = holder->device.get();
    hal_device_set_device(kernel_device, nullptr);
    check(device_stop(kernel_device) == ERROR_NONE);
    check(device_remove(kernel_device) == ERROR_NONE);
    check(device_destruct(kernel_device) == ERROR_NONE);
    holder->device = nullptr;
}

void registerDevice(const std::shared_ptr<Device>& device) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();

    if (device->getKernelDeviceHolder() == nullptr) {
        // Kernel device
        auto kernel_device_holder = createKernelDeviceHolder(device);
        device->setKernelDeviceHolder(kernel_device_holder);
    } else {
        LOGGER.warn("Device {} with id {} was already registered", device->getName(), device->getId());
    }
}

void deregisterDevice(const std::shared_ptr<Device>& device) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();

    // Kernel device
    auto kernel_device_holder = device->getKernelDeviceHolder();
    if (kernel_device_holder) {
        destroyKernelDeviceHolder(kernel_device_holder);
        device->setKernelDeviceHolder(nullptr);
    } else {
        LOGGER.warn("Device {} with id {} was not registered", device->getName(), device->getId());
    }
}

template<typename R>
auto toVector(R&& range) {
    using T = std::ranges::range_value_t<R>;
    std::vector<T> result;
    if constexpr (std::ranges::common_range<R>) {
        result.reserve(std::ranges::distance(range));
    }
    std::ranges::copy(range, std::back_inserter(result));
    return result;
}

std::vector<std::shared_ptr<Device>> findDevices(const std::function<bool(const std::shared_ptr<Device>&)>& filterFunction) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();

    auto devices_view = getDevices() | std::views::filter([&filterFunction](auto& device) {
        return filterFunction(device);
    });
    return toVector(devices_view);
}

std::shared_ptr<Device> findDevice(const std::function<bool(const std::shared_ptr<Device>&)>& filterFunction) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();

    auto result_set = getDevices() | std::views::filter([&filterFunction](auto& device) {
         return filterFunction(device);
    });
    if (!result_set.empty()) {
        return result_set.front();
    } else {
        return nullptr;
    }
}

std::shared_ptr<Device> findDevice(std::string name) {
    return findDevice([&name](auto& device){
        return device->getName() == name;
    });
}

std::shared_ptr<Device> findDevice(Device::Id id) {
    return findDevice([id](auto& device){
      return device->getId() == id;
    });
}

std::vector<std::shared_ptr<Device>> findDevices(Device::Type type) {
    return findDevices([type](auto& device) {
        return device->getType() == type;
    });
}

std::vector<std::shared_ptr<Device>> getDevices() {
    std::vector<std::shared_ptr<Device>> devices;
    device_for_each_of_type(&HAL_DEVICE_TYPE, &devices ,[](auto* kernelDevice, auto* context) {
        auto devices_ptr = static_cast<std::vector<std::shared_ptr<Device>>*>(context);
        auto hal_device = hal_device_get_device(kernelDevice);
        (*devices_ptr).push_back(hal_device);
        return true;
    });
    return devices;
}

bool hasDevice(Device::Type type) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();
    auto result_set = getDevices() | std::views::filter([&type](auto& device) {
         return device->getType() == type;
    });
    return !result_set.empty();
}

}
