// SPDX-License-Identifier: Apache-2.0
#include <tactility/hal/Device.h>
#include <tactility/driver.h>
#include <tactility/drivers/hal_device.hpp>

#include <Tactility/Logger.h>
#include <Tactility/RecursiveMutex.h>
#include <algorithm>

namespace tt::hal {

std::vector<std::shared_ptr<Device>> devices;
RecursiveMutex mutex;
static Device::Id nextId = 0;

static const auto LOGGER = Logger("Devices");

Device::Device() : id(nextId++) {}

template <std::ranges::range RangeType>
auto toVector(RangeType&& range) {
    auto view = range | std::views::common;
    return std::vector(view.begin(), view.end());
}

static std::shared_ptr<Device::KernelDeviceHolder> createKernelDeviceHolder(const std::shared_ptr<Device>& device) {
    auto kernel_device_holder = std::make_shared<Device::KernelDeviceHolder>(device->getName());
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

    if (findDevice(device->getId()) == nullptr) {
        devices.push_back(device);
        LOGGER.info("Registered {} with id {}", device->getName(), device->getId());
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
    }

    auto id_to_remove = device->getId();
    auto remove_iterator = std::remove_if(devices.begin(), devices.end(), [id_to_remove](const auto& device) {
        return device->getId() == id_to_remove;
    });
    if (remove_iterator != devices.end()) {
        LOGGER.info("Deregistering {} with id {}", device->getName(), device->getId());
        devices.erase(remove_iterator);
    } else {
        LOGGER.warn("Deregistering {} with id {} failed: not found", device->getName(), device->getId());
    }
}

std::vector<std::shared_ptr<Device>> findDevices(const std::function<bool(const std::shared_ptr<Device>&)>& filterFunction) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();

    auto devices_view = devices | std::views::filter([&filterFunction](auto& device) {
        return filterFunction(device);
    });
    return toVector(devices_view);
}

std::shared_ptr<Device> _Nullable findDevice(const std::function<bool(const std::shared_ptr<Device>&)>& filterFunction) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();

    auto result_set = devices | std::views::filter([&filterFunction](auto& device) {
         return filterFunction(device);
    });
    if (!result_set.empty()) {
        return result_set.front();
    } else {
        return nullptr;
    }
}

std::shared_ptr<Device> _Nullable findDevice(std::string name) {
    return findDevice([&name](auto& device){
        return device->getName() == name;
    });
}

std::shared_ptr<Device> _Nullable findDevice(Device::Id id) {
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
    return devices;
}

bool hasDevice(Device::Type type) {
    auto scoped_mutex = mutex.asScopedLock();
    scoped_mutex.lock();
    auto result_set = devices | std::views::filter([&type](auto& device) {
         return device->getType() == type;
    });
    return !result_set.empty();
}

}
