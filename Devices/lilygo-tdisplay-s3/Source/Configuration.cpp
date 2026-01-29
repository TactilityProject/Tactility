#include "devices/Display.h"
#include "devices/Power.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <ButtonControl.h>

bool initBoot();

using namespace tt::hal;

static std::vector<std::shared_ptr<Device>> createDevices() {
    return {
        createPower(),
        createDisplay(),
        ButtonControl::createTwoButtonControl(0, 14),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi {
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_7,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = GPIO_NUM_6,
                .max_transfer_sz = DISPLAY_HORIZONTAL_RESOLUTION * DISPLAY_VERTICAL_RESOLUTION * 2,
                .flags = 0
            },
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock()
        }
    }
};
