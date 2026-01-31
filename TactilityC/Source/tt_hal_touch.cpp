#include "tt_hal_touch.h"

#include <tactility/hal/Device.h>
#include "Tactility/hal/touch/TouchDevice.h"
#include "Tactility/hal/touch/TouchDriver.h"

struct DriverWrapper {
    std::shared_ptr<tt::hal::touch::TouchDriver> driver;
    DriverWrapper(std::shared_ptr<tt::hal::touch::TouchDriver> driver) : driver(driver) {}
};

static std::shared_ptr<tt::hal::touch::TouchDevice> findValidTouchDevice(tt::hal::Device::Id id) {
    auto device = tt::hal::findDevice(id);
    if (device == nullptr || device->getType() != tt::hal::Device::Type::Touch) {
        return nullptr;
    }
    return std::reinterpret_pointer_cast<tt::hal::touch::TouchDevice>(device);
}

extern "C" {

bool tt_hal_touch_driver_supported(DeviceId id) {
    auto touch = findValidTouchDevice(id);
    return touch != nullptr && touch->supportsTouchDriver();
}

TouchDriverHandle tt_hal_touch_driver_alloc(DeviceId id) {
    auto touch = findValidTouchDevice(id);
    assert(touch->supportsTouchDriver());
    return new DriverWrapper(touch->getTouchDriver());
}

void tt_hal_touch_driver_free(TouchDriverHandle handle) {
    DriverWrapper* wrapper = static_cast<DriverWrapper*>(handle);
    delete wrapper;
}

bool tt_hal_touch_driver_get_touched_points(TouchDriverHandle handle, uint16_t* x, uint16_t* y, uint16_t* _Nullable strength, uint8_t* pointCount, uint8_t maxPointCount) {
    DriverWrapper* wrapper = static_cast<DriverWrapper*>(handle);
    return wrapper->driver->getTouchedPoints(x, y, strength, pointCount, maxPointCount);
}

}
