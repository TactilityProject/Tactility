#include "InitBoot.h"
#include "devices/Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/uart/Uart.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Axp2101Power.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    return {
        axp2101,
        aw9523,
        std::make_shared<Axp2101Power>(axp2101),
        createSdCard(),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi {
        spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_37,
                .miso_io_num = GPIO_NUM_35,
                .sclk_io_num = GPIO_NUM_36,
                .data2_io_num = GPIO_NUM_NC,
                .data3_io_num = GPIO_NUM_NC,
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
            .lock = tt::lvgl::getSyncLock() // esp_lvgl_port owns the lock for the display
        }
    }
};
