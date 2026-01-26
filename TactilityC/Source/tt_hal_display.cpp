#include "tt_hal_display.h"

#include "Tactility/Check.h"
#include "Tactility/hal/Device.h"
#include "Tactility/hal/display/DisplayDevice.h"
#include "Tactility/hal/display/DisplayDriver.h"

static ColorFormat toColorFormat(tt::hal::display::ColorFormat format) {
    switch (format) {
        case tt::hal::display::ColorFormat::Monochrome:
            return COLOR_FORMAT_MONOCHROME;
        case tt::hal::display::ColorFormat::BGR565:
            return COLOR_FORMAT_BGR565;
        case tt::hal::display::ColorFormat::BGR565Swapped:
            return COLOR_FORMAT_BGR565_SWAPPED;
        case tt::hal::display::ColorFormat::RGB565:
            return COLOR_FORMAT_RGB565;
        case tt::hal::display::ColorFormat::RGB565Swapped:
            return COLOR_FORMAT_RGB565_SWAPPED;
        case tt::hal::display::ColorFormat::RGB888:
            return COLOR_FORMAT_RGB888;
        default:
            check(false, "ColorFormat not supported");
    }
}

struct DriverWrapper {
    std::shared_ptr<tt::hal::display::DisplayDriver> driver;
    DriverWrapper(std::shared_ptr<tt::hal::display::DisplayDriver> driver) : driver(driver) {}
};

static std::shared_ptr<tt::hal::display::DisplayDevice> findValidDisplayDevice(tt::hal::Device::Id id) {
    auto device = tt::hal::findDevice(id);
    if (device == nullptr || device->getType() != tt::hal::Device::Type::Display) {
        return nullptr;
    }
    return std::reinterpret_pointer_cast<tt::hal::display::DisplayDevice>(device);
}

extern "C" {

bool tt_hal_display_driver_supported(DeviceId id) {
    auto display = findValidDisplayDevice(id);
    return display != nullptr && display->supportsDisplayDriver();
}

DisplayDriverHandle tt_hal_display_driver_alloc(DeviceId id) {
    auto display = findValidDisplayDevice(id);
    assert(display->supportsDisplayDriver());
    return new DriverWrapper(display->getDisplayDriver());
}

void tt_hal_display_driver_free(DisplayDriverHandle handle) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    delete wrapper;
}

bool tt_hal_display_driver_lock(DisplayDriverHandle handle, TickType_t timeout) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    return wrapper->driver->getLock()->lock(timeout);
}

void tt_hal_display_driver_unlock(DisplayDriverHandle handle) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    wrapper->driver->getLock()->unlock();
}

ColorFormat tt_hal_display_driver_get_colorformat(DisplayDriverHandle handle) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    return toColorFormat(wrapper->driver->getColorFormat());
}

uint16_t tt_hal_display_driver_get_pixel_width(DisplayDriverHandle handle) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    return wrapper->driver->getPixelWidth();
}

uint16_t tt_hal_display_driver_get_pixel_height(DisplayDriverHandle handle) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    return wrapper->driver->getPixelHeight();
}

void tt_hal_display_driver_draw_bitmap(DisplayDriverHandle handle, int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) {
    auto wrapper = static_cast<DriverWrapper*>(handle);
    wrapper->driver->drawBitmap(xStart, yStart, xEnd, yEnd, pixelData);
}

}