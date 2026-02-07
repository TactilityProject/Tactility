#include "devices/Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <TCA9534.h>

using namespace tt::hal;

static bool initBoot() {
    TCA9534_IO_EXP io_expander = {
        .I2C_ADDR = 0x18,
        .i2c_master_port = I2C_NUM_0,
        .interrupt_pin = GPIO_NUM_NC,
        .interrupt_task = nullptr
    };

    // Enable LCD backlight
    set_tca9534_io_pin_direction(&io_expander, TCA9534_IO1, TCA9534_OUTPUT);
    set_tca9534_io_pin_output_state(&io_expander, TCA9534_IO1, 255);

    return true;
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi {
        // SD card
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_6,
                .miso_io_num = GPIO_NUM_4,
                .sclk_io_num = GPIO_NUM_5,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .data4_io_num = GPIO_NUM_NC,
                .data5_io_num = GPIO_NUM_NC,
                .data6_io_num = GPIO_NUM_NC,
                .data7_io_num = GPIO_NUM_NC,
                .data_io_default_level = false,
                .max_transfer_sz = 8192,
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = nullptr // No custom lock needed
        }
    }
};
