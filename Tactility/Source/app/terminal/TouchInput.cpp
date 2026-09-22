#include <Tactility/app/terminal/TouchInput.h>

#include <tactility/device.h>
#include <tactility/drivers/pointer.h>

TouchInput::TouchInput(bool keyboardPresent) {
    if (keyboardPresent) {
        return;
    }
    if (device_get_first_active_by_type(&POINTER_TYPE, &device_) != ERROR_NONE) {
        device_ = nullptr;
    }
}

TouchInput::~TouchInput() {
    if (device_ != nullptr) {
        device_put(device_);
    }
}

bool TouchInput::touched() const {
    if (device_ == nullptr || pointer_read_data(device_, 0) != ERROR_NONE) {
        return false;
    }
    uint16_t x, y, strength;
    uint8_t pointCount = 0;
    return pointer_get_touched_points(device_, &x, &y, &strength, &pointCount, 1);
}
