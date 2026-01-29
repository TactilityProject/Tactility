#include "devices/Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static bool initBoot() {
    // Note: I tried 100 Hz to 100 kHz and couldn't get the flickering to stop
    return driver::pwmbacklight::init(GPIO_NUM_2);
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
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
                .mosi_io_num = GPIO_NUM_11,
                .miso_io_num = GPIO_NUM_13,
                .sclk_io_num = GPIO_NUM_12,
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
            .lock = nullptr
        }
    },
    .uart {
        // "UART1"
        uart::Configuration {
            .name = "UART1",
            .port = UART_NUM_1,
            .rxPin = GPIO_NUM_44,
            .txPin = GPIO_NUM_43,
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
