// SPDX-License-Identifier: Apache-2.0
#include <Tactility/hal/display/KernelDisplayDriver.h>

#include <tactility/drivers/display.h>

#include <algorithm>
#include <cassert>
#include <utility>

namespace tt::hal::display {

static ColorFormat toColorFormat(DisplayColorFormat format) {
    switch (format) {
        case DISPLAY_COLOR_FORMAT_MONOCHROME:
            return ColorFormat::Monochrome;
        case DISPLAY_COLOR_FORMAT_BGR565:
            return ColorFormat::BGR565;
        case DISPLAY_COLOR_FORMAT_BGR565_SWAPPED:
            return ColorFormat::BGR565Swapped;
        case DISPLAY_COLOR_FORMAT_RGB565:
            return ColorFormat::RGB565;
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
            return ColorFormat::RGB565Swapped;
        case DISPLAY_COLOR_FORMAT_RGB888:
            return ColorFormat::RGB888;
        default:
            std::unreachable();
    }
}

KernelDisplayDriver::KernelDisplayDriver(::Device* device) : device(device) {
    assert(device_get_type(device) == &DISPLAY_TYPE);
}

ColorFormat KernelDisplayDriver::getColorFormat() const {
    return toColorFormat(display_get_color_format(device));
}

uint16_t KernelDisplayDriver::getPixelWidth() const {
    return display_get_resolution_x(device);
}

uint16_t KernelDisplayDriver::getPixelHeight() const {
    return display_get_resolution_y(device);
}

bool KernelDisplayDriver::drawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) {
    return display_draw_bitmap(device, xStart, yStart, xEnd, yEnd, pixelData) == ERROR_NONE;
}

uint8_t KernelDisplayDriver::getFrameBuffers(void* outBuffers[2]) const {
    uint8_t count = std::min<uint8_t>(display_get_frame_buffer_count(device), 2);
    for (uint8_t i = 0; i < count; i++) {
        display_get_frame_buffer(device, i, &outBuffers[i]);
    }
    return count;
}

}
