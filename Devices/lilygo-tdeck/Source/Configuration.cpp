#include "devices/Display.h"
#include "devices/KeyboardBacklight.h"
#include "devices/Power.h"
#include "devices/Sdcard.h"
#include "devices/TdeckKeyboard.h"
#include "devices/TrackballDevice.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>

bool initBoot();

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createPower(),
        createDisplay(),
        std::make_shared<TdeckKeyboard>(),
        std::make_shared<KeyboardBacklightDevice>(),
        std::make_shared<TrackballDevice>(),
        createSdCard()
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
                .mosi_io_num = GPIO_NUM_41,
                .miso_io_num = GPIO_NUM_38,
                .sclk_io_num = GPIO_NUM_40,
                .quadwp_io_num = GPIO_NUM_NC, // Quad SPI LCD driver is not yet supported
                .quadhd_io_num = GPIO_NUM_NC, // Quad SPI LCD driver is not yet supported
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
    },
    .uart {
        uart::Configuration {
            .name = "Grove",
            .port = UART_NUM_1,
            .rxPin = GPIO_NUM_44,
            .txPin = GPIO_NUM_43,
            .rtsPin = GPIO_NUM_NC,
            .ctsPin = GPIO_NUM_NC,
            .rxBufferSize = 1024,
            .txBufferSize = 1024,
            .config = {
                .baud_rate = 38400,
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
