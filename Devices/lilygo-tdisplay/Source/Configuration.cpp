#include "devices/Display.h"

#include <PwmBacklight.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <ButtonControl.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT);
}

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createDisplay(),
        ButtonControl::createTwoButtonControl(35, 0)
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiScale = UiScale::Smallest,
    .createDevices = createDevices,
    .spi {
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_19,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = GPIO_NUM_18,
                .quadwp_io_num = GPIO_NUM_NC,
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
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock()
        }
    }
};
