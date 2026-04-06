#include "Xpt2046SoftSpi.h"

#include <Tactility/Logger.h>
#include <Tactility/settings/TouchCalibrationSettings.h>

#include <algorithm>

#include <driver/gpio.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

static const auto LOGGER = tt::Logger("Xpt2046SoftSpi");

constexpr auto CMD_READ_Y = 0x90;
constexpr auto CMD_READ_X = 0xD0;

constexpr int RAW_MIN_DEFAULT = 100;
constexpr int RAW_MAX_DEFAULT = 1900;
constexpr int RAW_VALID_MIN = 100;
constexpr int RAW_VALID_MAX = 3900;

Xpt2046SoftSpi::Xpt2046SoftSpi(std::unique_ptr<Configuration> inConfiguration)
    : configuration(std::move(inConfiguration)) {
    assert(configuration != nullptr);
}

bool Xpt2046SoftSpi::start() {
    LOGGER.info("Starting Xpt2046SoftSpi touch driver");

    // Configure GPIO pins
    gpio_config_t io_conf = {};

    // Configure MOSI, CLK, CS as outputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << configuration->mosiPin) |
        (1ULL << configuration->clkPin) |
        (1ULL << configuration->csPin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    if (gpio_config(&io_conf) != ESP_OK) {
        LOGGER.error("Failed to configure output pins");
        return false;
    }

    // Configure MISO as input
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << configuration->misoPin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;

    if (gpio_config(&io_conf) != ESP_OK) {
        LOGGER.error("Failed to configure input pin");
        return false;
    }

    // Initialize pin states
    gpio_set_level(configuration->csPin, 1); // CS high
    gpio_set_level(configuration->clkPin, 0); // CLK low
    gpio_set_level(configuration->mosiPin, 0); // MOSI low

    LOGGER.info(
        "GPIO configured: MOSI={}, MISO={}, CLK={}, CS={}",
        static_cast<int>(configuration->mosiPin),
        static_cast<int>(configuration->misoPin),
        static_cast<int>(configuration->clkPin),
        static_cast<int>(configuration->csPin)
    );

    return true;
}

bool Xpt2046SoftSpi::stop() {
    LOGGER.info("Stopping Xpt2046SoftSpi touch driver");

    // Stop LVLG if needed
    if (lvglDevice != nullptr) {
        stopLvgl();
    }

    return true;
}

bool Xpt2046SoftSpi::startLvgl(lv_display_t* display) {
    (void)display;
    if (lvglDevice != nullptr) {
        LOGGER.error("LVGL was already started");
        return false;
    }

    lvglDevice = lv_indev_create();
    if (lvglDevice == nullptr) {
        LOGGER.error("Failed to create LVGL input device");
        return false;
    }

    lv_indev_set_type(lvglDevice, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvglDevice, touchReadCallback);
    lv_indev_set_user_data(lvglDevice, this);

    LOGGER.info("Xpt2046SoftSpi touch driver started successfully");
    return true;
}

bool Xpt2046SoftSpi::stopLvgl() {
    if (lvglDevice != nullptr) {
        lv_indev_delete(lvglDevice);
        lvglDevice = nullptr;
    }
    return true;
}

int Xpt2046SoftSpi::readSPI(uint8_t command) {
    int result = 0;

    // Pull CS low for this transaction
    gpio_set_level(configuration->csPin, 0);
    ets_delay_us(1);

    // Send 8-bit command
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(configuration->mosiPin, (command & (1 << i)) ? 1 : 0);
        gpio_set_level(configuration->clkPin, 1);
        ets_delay_us(1);
        gpio_set_level(configuration->clkPin, 0);
        ets_delay_us(1);
    }

    for (int i = 11; i >= 0; i--) {
        gpio_set_level(configuration->clkPin, 1);
        ets_delay_us(1);
        if (gpio_get_level(configuration->misoPin)) {
            result |= (1 << i);
        }
        gpio_set_level(configuration->clkPin, 0);
        ets_delay_us(1);
    }

    // Pull CS high for this transaction
    gpio_set_level(configuration->csPin, 1);

    return result;
}

bool Xpt2046SoftSpi::readRawPoint(uint16_t& x, uint16_t& y) {
    constexpr int sampleCount = 8;
    int totalX = 0;
    int totalY = 0;
    int validSamples = 0;

    for (int i = 0; i < sampleCount; i++) {
        const int rawX = readSPI(CMD_READ_X);
        const int rawY = readSPI(CMD_READ_Y);

        if (rawX > RAW_VALID_MIN && rawX < RAW_VALID_MAX && rawY > RAW_VALID_MIN && rawY < RAW_VALID_MAX) {
            totalX += rawX;
            totalY += rawY;
            validSamples++;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (validSamples < 3) {
        return false;
    }

    x = static_cast<uint16_t>(totalX / validSamples);
    y = static_cast<uint16_t>(totalY / validSamples);
    return true;
}

bool Xpt2046SoftSpi::getTouchPoint(Point& point) {
    uint16_t rawX = 0;
    uint16_t rawY = 0;
    if (!readRawPoint(rawX, rawY)) {
        return false;
    }

    int mappedX = (static_cast<int>(rawX) - RAW_MIN_DEFAULT) * static_cast<int>(configuration->xMax) /
        (RAW_MAX_DEFAULT - RAW_MIN_DEFAULT);
    int mappedY = (static_cast<int>(rawY) - RAW_MIN_DEFAULT) * static_cast<int>(configuration->yMax) /
        (RAW_MAX_DEFAULT - RAW_MIN_DEFAULT);

    if (configuration->swapXy) {
        std::swap(mappedX, mappedY);
    }
    if (configuration->mirrorX) {
        mappedX = static_cast<int>(configuration->xMax) - mappedX;
    }
    if (configuration->mirrorY) {
        mappedY = static_cast<int>(configuration->yMax) - mappedY;
    }

    uint16_t x = static_cast<uint16_t>(std::clamp(mappedX, 0, static_cast<int>(configuration->xMax)));
    uint16_t y = static_cast<uint16_t>(std::clamp(mappedY, 0, static_cast<int>(configuration->yMax)));

    const auto calibration = tt::settings::touch::getActive();
    tt::settings::touch::applyCalibration(calibration, configuration->xMax, configuration->yMax, x, y);

    point.x = x;
    point.y = y;
    return true;
}

bool Xpt2046SoftSpi::isTouched() {
    uint16_t x = 0;
    uint16_t y = 0;
    return readRawPoint(x, y);
}

void Xpt2046SoftSpi::touchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* touch = static_cast<Xpt2046SoftSpi*>(lv_indev_get_user_data(indev));
    if (touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    Point point;
    if (touch->getTouchPoint(point)) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Return driver instance if any
std::shared_ptr<tt::hal::touch::TouchDriver> Xpt2046SoftSpi::getTouchDriver() {
    assert(lvglDevice == nullptr); // Still attached to LVGL context. Call stopLvgl() first.

    if (touchDriver == nullptr) {
        touchDriver = std::make_shared<Xpt2046SoftSpiDriver>(this);
    }

    return touchDriver;
}
