#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/CardputerKeyboard.h"
#include "devices/CardputerPower.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>

#include <PwmBacklight.h>
#include <Tca8418.h>

using namespace tt::hal;

bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT, 512);
}

static DeviceVector createDevices() {
    auto tca8418 = std::make_shared<Tca8418>(I2C_NUM_0);
    return {
        createSdCard(),
        createDisplay(),
        tca8418,
        std::make_shared<CardputerKeyboard>(tca8418),
        std::make_shared<CardputerPower>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiScale = UiScale::Smallest,
    .createDevices = createDevices,
    .spi {
        // Display
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_35,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = GPIO_NUM_36,
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
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock()
        },
        // SDCard
        spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_14,
                .miso_io_num = GPIO_NUM_39,
                .sclk_io_num = GPIO_NUM_40,
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
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = nullptr
        },

    },
    .uart {
        uart::Configuration {
            .name = "Port A",
            .port = UART_NUM_1,
            .rxPin = GPIO_NUM_2,
            .txPin = GPIO_NUM_1,
            .rtsPin = GPIO_NUM_NC,
            .ctsPin = GPIO_NUM_NC,
            .rxBufferSize = 1024,
            .txBufferSize = 1024,
            .config = {
                .baud_rate = 115200,
                .data_bits = UART_DATA_8_BITS,
                .parity    = UART_PARITY_DISABLE,
                .stop_bits = UART_STOP_BITS_1,
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                .rx_flow_ctrl_thresh = 0,
                .source_clk = UART_SCLK_DEFAULT,
                .flags = {
                    .allow_pd = 0,
                    .backup_before_sleep = 0,
                }
            }
        }
    }
};
