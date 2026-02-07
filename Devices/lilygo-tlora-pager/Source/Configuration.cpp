#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/TpagerEncoder.h"
#include "devices/TpagerKeyboard.h"
#include "devices/TpagerPower.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Bq25896.h>
#include <Drv2605.h>

#define TPAGER_SPI_TRANSFER_SIZE_LIMIT (480 * 222 * (LV_COLOR_DEPTH / 8))

bool tpagerInit();

using namespace tt::hal;

static DeviceVector createDevices() {
    auto bq27220 = std::make_shared<Bq27220>(I2C_NUM_0);
    auto power = std::make_shared<TpagerPower>(bq27220);

    auto tca8418 = std::make_shared<Tca8418>(I2C_NUM_0);
    auto keyboard = std::make_shared<TpagerKeyboard>(tca8418);

    return std::vector<std::shared_ptr<tt::hal::Device>> {
        tca8418,
        std::make_shared<Bq25896>(I2C_NUM_0),
        bq27220,
        std::make_shared<Drv2605>(I2C_NUM_0),
        power,
        createTpagerSdCard(),
        createDisplay(),
        keyboard,
        std::make_shared<TpagerEncoder>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = tpagerInit,
    .createDevices = createDevices,
    .spi {spi::Configuration {
        .device = SPI2_HOST,
        .dma = SPI_DMA_CH_AUTO,
        .config = {
            .mosi_io_num = GPIO_NUM_34,
            .miso_io_num = GPIO_NUM_33,
            .sclk_io_num = GPIO_NUM_35,
            .quadwp_io_num = GPIO_NUM_NC, // Quad SPI LCD driver is not yet supported
            .quadhd_io_num = GPIO_NUM_NC, // Quad SPI LCD driver is not yet supported
            .data4_io_num = GPIO_NUM_NC,
            .data5_io_num = GPIO_NUM_NC,
            .data6_io_num = GPIO_NUM_NC,
            .data7_io_num = GPIO_NUM_NC,
            .data_io_default_level = false,
            .max_transfer_sz = TPAGER_SPI_TRANSFER_SIZE_LIMIT,
            .flags = 0,
            .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
            .intr_flags = 0
        },
        .initMode = spi::InitMode::ByTactility,
        .isMutable = false,
        .lock = tt::lvgl::getSyncLock() // esp_lvgl_port owns the lock for the display
    }}
};
