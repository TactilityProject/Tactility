// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/hal_device.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#include <tactility/log.h>
#include <tactility/hal/Device.h>
#include <memory>
#include <utility>

#define TAG LOG_TAG(HalDevice)

struct HalDevicePrivate {
    std::shared_ptr<tt::hal::Device> halDevice;
};

#define GET_DATA(device) ((struct HalDevicePrivate*)device->internal.driver_data)

static enum HalDeviceType getHalDeviceType(tt::hal::Device::Type type) {
    switch (type) {
        case tt::hal::Device::Type::I2c:
            return HalDeviceType::HAL_DEVICE_TYPE_I2C;
        case tt::hal::Device::Type::Display:
            return HalDeviceType::HAL_DEVICE_TYPE_DISPLAY;
        case tt::hal::Device::Type::Touch:
            return HalDeviceType::HAL_DEVICE_TYPE_TOUCH;
        case tt::hal::Device::Type::SdCard:
            return HalDeviceType::HAL_DEVICE_TYPE_SDCARD;
        case tt::hal::Device::Type::Keyboard:
            return HalDeviceType::HAL_DEVICE_TYPE_KEYBOARD;
        case tt::hal::Device::Type::Encoder:
            return HalDeviceType::HAL_DEVICE_TYPE_ENCODER;
        case tt::hal::Device::Type::Power:
            return HalDeviceType::HAL_DEVICE_TYPE_POWER;
        case tt::hal::Device::Type::Gps:
            return HalDeviceType::HAL_DEVICE_TYPE_GPS;
        case tt::hal::Device::Type::Other:
            return HalDeviceType::HAL_DEVICE_TYPE_OTHER;
        default:
            LOG_W(TAG, "Device type %d is not implemented", static_cast<int>(type));
            return HalDeviceType::HAL_DEVICE_TYPE_OTHER;
    }
}

HalDeviceType hal_device_get_type(struct Device* device) {
    auto type = GET_DATA(device)->halDevice->getType();
    return getHalDeviceType(type);
}

void hal_device_for_each_of_type(HalDeviceType type, void* context, bool(*onDevice)(struct Device* device, void* context)) {
    struct InternalContext {
        HalDeviceType typeParam;
        void* contextParam;
        bool(*onDeviceParam)(struct Device* device, void* context);
    };

    InternalContext internal_context = {
        .typeParam = type,
        .contextParam = context,
        .onDeviceParam = onDevice
    };

    for_each_device_of_type(&HAL_DEVICE_TYPE, &internal_context, [](Device* device, void* context){
        auto* hal_device_private = GET_DATA(device);
        auto* internal_context = static_cast<InternalContext*>(context);
        auto hal_device_type = getHalDeviceType(hal_device_private->halDevice->getType());
        if (hal_device_type == internal_context->typeParam) {
            if (!internal_context->onDeviceParam(device, internal_context->contextParam)) {
                return false;
            }
        }
        return true;
    });
}

namespace tt::hal {

std::shared_ptr<Device> hal_device_get_device(::Device* device) {
    auto* hal_device_private = GET_DATA(device);
    return hal_device_private->halDevice;
}

void hal_device_set_device(::Device* kernelDevice, std::shared_ptr<Device> halDevice) {
    GET_DATA(kernelDevice)->halDevice = std::move(halDevice);
}

}

#pragma region Lifecycle

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    device->internal.driver_data = new HalDevicePrivate();
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    delete GET_DATA(device);
    return ERROR_NONE;
}

#pragma endregion

extern "C" {

const struct DeviceType HAL_DEVICE_TYPE {0};

extern struct Module hal_device_module;

Driver hal_device_driver = {
    .name = "hal-device",
    .compatible = (const char*[]) {"hal-device", nullptr},
    .startDevice = start,
    .stopDevice = stop,
    .api = nullptr,
    .deviceType = &HAL_DEVICE_TYPE,
    .owner = &hal_device_module,
    .driver_private = nullptr
};

}