// SPDX-License-Identifier: Apache-2.0
#include <Tactility/hal/touch/KernelTouchDriver.h>

#include <tactility/drivers/touch.h>

#include <cassert>

namespace tt::hal::touch {

// Bus reads are expected to complete quickly; bound the wait so a stalled controller can't block the LVGL indev poll.
static constexpr TickType_t READ_TIMEOUT = pdMS_TO_TICKS(10);

KernelTouchDriver::KernelTouchDriver(::Device* device) : device(device) {
    assert(device_get_type(device) == &TOUCH_TYPE);
}

bool KernelTouchDriver::getTouchedPoints(uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* pointCount, uint8_t maxPointCount) {
    if (touch_read_data(device, READ_TIMEOUT) != ERROR_NONE) {
        return false;
    }
    return touch_get_touched_points(device, x, y, strength, pointCount, maxPointCount);
}

}
