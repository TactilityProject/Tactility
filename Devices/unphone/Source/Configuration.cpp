#include "UnPhoneFeatures.h"
#include "devices/Hx8357Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Xpt2046Power.h>

#define UNPHONE_SPI_TRANSFER_SIZE_LIMIT (UNPHONE_LCD_HORIZONTAL_RESOLUTION * UNPHONE_LCD_SPI_TRANSFER_HEIGHT * LV_COLOR_DEPTH / 8)

bool initBoot();

static tt::hal::DeviceVector createDevices() {
    return {
        std::make_shared<Xpt2046Power>(),
        createDisplay(),
        createSdCard()
    };
}

extern const tt::hal::Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi {
        tt::hal::spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_40,
                .miso_io_num = GPIO_NUM_41,
                .sclk_io_num = GPIO_NUM_39,
                .quadwp_io_num = -1, // Quad SPI LCD driver is not yet supported
                .quadhd_io_num = -1, // Quad SPI LCD driver is not yet supported
                .data4_io_num = GPIO_NUM_NC,
                .data5_io_num = GPIO_NUM_NC,
                .data6_io_num = GPIO_NUM_NC,
                .data7_io_num = GPIO_NUM_NC,
                .data_io_default_level = false,
                .max_transfer_sz = UNPHONE_SPI_TRANSFER_SIZE_LIMIT,
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = tt::hal::spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock() // esp_lvgl_port owns the lock for the display
        }
    }
};
