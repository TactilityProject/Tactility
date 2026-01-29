#include "devices/Display.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <ButtonControl.h>
#include <PwmBacklight.h>

using namespace tt::hal;

bool initBoot() {
    // CH552 applies 4 V to GPIO 0, which reduces Wi-Fi sensitivity
    // Setting output to high adds a bias of 3.3 V and suppresses over-voltage:
    gpio::configure(0, gpio::Mode::Output, false, false);
    gpio::setLevel(0, true);

    // "Hold power" pin: must be set to high to keep the device powered on:
    gpio::configure(4, gpio::Mode::Output, false, false);
    gpio::setLevel(4, true);
    return driver::pwmbacklight::init(GPIO_NUM_27, 512);
}

static DeviceVector createDevices() {
    return {
        ButtonControl::createTwoButtonControl(37, 39),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiScale = UiScale::Smallest,
    .createDevices = createDevices,
    .spi {
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_15,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = GPIO_NUM_13,
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
            .lock = tt::lvgl::getSyncLock() // esp_lvgl_port owns the lock for the display
        }
    },
    .uart {
        uart::Configuration {
            .name = "Grove",
            .port = UART_NUM_1,
            .rxPin = GPIO_NUM_32,
            .txPin = GPIO_NUM_33,
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
        },
    }
};
