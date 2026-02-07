#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/lvgl/LvglSync.h>

#include <PwmBacklight.h>

bool initBoot() {
    if (!driver::pwmbacklight::init(LCD_PIN_BACKLIGHT)) {
        return false;
    }

    // Set the RGB LED Pins to output and turn them off
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT)); // Red
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT)); // Green
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT)); // Blue

    // 0 on, 1 off
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 1)); // Red
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_16, 1)); // Green
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_17, 1)); // Blue

    // This display has a weird glitch with gamma during boot, which results in uneven dark gray colours.
    // Setting gamma curve index to 0 doesn't work at boot for an unknown reason, so we set the curve index to 1:
    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](auto) {
        auto display = tt::hal::findFirstDevice<tt::hal::display::DisplayDevice>(tt::hal::Device::Type::Display);
        assert(display != nullptr);
        tt::lvgl::lock(portMAX_DELAY);
        display->setGammaCurve(1U);
        tt::lvgl::unlock();
    });

    return true;
}

static tt::hal::DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const tt::hal::Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi {
        // Display
        tt::hal::spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_13,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = GPIO_NUM_14,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .data4_io_num = GPIO_NUM_NC,
                .data5_io_num = GPIO_NUM_NC,
                .data6_io_num = GPIO_NUM_NC,
                .data7_io_num = GPIO_NUM_NC,
                .data_io_default_level = false,
                .max_transfer_sz = LCD_SPI_TRANSFER_SIZE_LIMIT,
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = tt::hal::spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock() // esp_lvgl_port owns the lock for the display
        },
        // SD card
        tt::hal::spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_23,
                .miso_io_num = GPIO_NUM_19,
                .sclk_io_num = GPIO_NUM_18,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .data4_io_num = GPIO_NUM_NC,
                .data5_io_num = GPIO_NUM_NC,
                .data6_io_num = GPIO_NUM_NC,
                .data7_io_num = GPIO_NUM_NC,
                .data_io_default_level = false,
                .max_transfer_sz = 0,
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = tt::hal::spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = nullptr
        },
    }
};
