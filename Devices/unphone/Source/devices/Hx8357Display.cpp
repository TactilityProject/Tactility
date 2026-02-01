#include "Hx8357Display.h"
#include "Touch.h"

#include <UnPhoneFeatures.h>
#include <Tactility/Logger.h>

#include <hx8357/disp_spi.h>
#include <hx8357/hx8357.h>

static const auto LOGGER = tt::Logger("Hx8357Display");

constexpr auto BUFFER_SIZE = (UNPHONE_LCD_HORIZONTAL_RESOLUTION * UNPHONE_LCD_DRAW_BUFFER_HEIGHT * LV_COLOR_DEPTH / 8);

extern std::shared_ptr<UnPhoneFeatures> unPhoneFeatures;

bool Hx8357Display::start() {
    LOGGER.info("start");

    disp_spi_add_device(SPI2_HOST);

    hx8357_reset(GPIO_NUM_46);
    hx8357_init(UNPHONE_LCD_PIN_DC);
    uint8_t madctl = (1U << MADCTL_BIT_INDEX_COLUMN_ADDRESS_ORDER);
    hx8357_set_madctl(madctl);

    return true;
}

bool Hx8357Display::stop() {
    LOGGER.info("stop");
    disp_spi_remove_device();
    return true;
}

bool Hx8357Display::startLvgl() {
    LOGGER.info("startLvgl");

    if (lvglDisplay != nullptr) {
        LOGGER.warn("LVGL was already started");
        return false;
    }

    lvglDisplay = lv_display_create(UNPHONE_LCD_HORIZONTAL_RESOLUTION, UNPHONE_LCD_VERTICAL_RESOLUTION);
    lv_display_set_physical_resolution(lvglDisplay, UNPHONE_LCD_HORIZONTAL_RESOLUTION, UNPHONE_LCD_VERTICAL_RESOLUTION);
    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_NATIVE);

    // TODO malloc to use SPIRAM
    buffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA));
    assert(buffer != nullptr);

    lv_display_set_buffers(
        lvglDisplay,
        buffer,
        nullptr,
        BUFFER_SIZE,
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    lv_display_set_flush_cb(lvglDisplay, hx8357_flush);

    if (lvglDisplay == nullptr) {
        LOGGER.info("Failed");
        return false;
    }

    unPhoneFeatures->setBacklightPower(true);

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->startLvgl(lvglDisplay);
    }

    return true;
}

bool Hx8357Display::stopLvgl() {
    LOGGER.info("stopLvgl");

    if (lvglDisplay == nullptr) {
        LOGGER.warn("LVGL was already stopped");
        return false;
    }

    // Just in case
    disp_wait_for_pending_transactions();

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr && touch_device->getLvglIndev() != nullptr) {
        LOGGER.info("Stopping touch device");
        touch_device->stopLvgl();
    }

    lv_display_delete(lvglDisplay);
    lvglDisplay = nullptr;

    heap_caps_free(buffer);
    buffer = nullptr;

    return true;
}

std::shared_ptr<tt::hal::touch::TouchDevice> Hx8357Display::getTouchDevice() {
    if (touchDevice == nullptr) {
        touchDevice = std::reinterpret_pointer_cast<tt::hal::touch::TouchDevice>(createTouch());
        LOGGER.info("Created touch device");
    }

    return touchDevice;
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    return std::make_shared<Hx8357Display>();
}

bool Hx8357Display::Hx8357Driver::drawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) {
    lv_area_t area = { xStart, yStart, xEnd, yEnd };
    hx8357_flush(nullptr, &area, (uint8_t*)pixelData);
    return true;
}
