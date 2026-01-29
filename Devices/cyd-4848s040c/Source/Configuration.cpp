#include "devices/St7701Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/lvgl/LvglSync.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(GPIO_NUM_38, 1000);
}

static DeviceVector createDevices() {
    return {
        std::make_shared<St7701Display>(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi {
        // SD Card & display init
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_47,
                .miso_io_num = GPIO_NUM_41,
                .sclk_io_num = GPIO_NUM_48,
                .quadwp_io_num = -1,
                .quadhd_io_num = GPIO_NUM_42,
                .data4_io_num = -1,
                .data5_io_num = -1,
                .data6_io_num = -1,
                .data7_io_num = -1,
                .data_io_default_level = false,
                .max_transfer_sz = 1024 * 128,
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
