#include "devices/Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static constexpr int SPI_TRANSFER_SIZE_LIMIT = 320 * 10;

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

static bool initBoot() {
    return driver::pwmbacklight::init(GPIO_NUM_1);
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    //I2C Internal - Touch
    //I2C External - P3 (JST SH 1.0)/ P4 (JST SH 1.25) headers - GND 3.3V IO17 IO18
    .spi {
        //Display
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .data0_io_num = GPIO_NUM_21,
                .data1_io_num = GPIO_NUM_48,
                .sclk_io_num = GPIO_NUM_47,
                .data2_io_num = GPIO_NUM_40,
                .data3_io_num = GPIO_NUM_39,
                .data4_io_num = -1,
                .data5_io_num = -1,
                .data6_io_num = -1,
                .data7_io_num = -1,
                .data_io_default_level = false,
                .max_transfer_sz = SPI_TRANSFER_SIZE_LIMIT,
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock()
        },
        //SD Card
        spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_11,
                .miso_io_num = GPIO_NUM_13,
                .sclk_io_num = GPIO_NUM_12,
                .quadwp_io_num = -1,
                .quadhd_io_num = -1,
                .data4_io_num = -1,
                .data5_io_num = -1,
                .data6_io_num = -1,
                .data7_io_num = -1,
                .data_io_default_level = false,
                .max_transfer_sz = 8192,
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = nullptr
        }
    }
};
