#include "devices/Display.h"
#include "devices/SdCard.h"

#include <PwmBacklight.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>

using namespace tt::hal;

static bool initBoot() {
    //Set the RGB Led Pins to output and turn them off
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT)); //Red
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT)); //Green
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT)); //Blue

    //0 on, 1 off... yep it's backwards.
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 1)); //Red
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_16, 1)); //Green
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_17, 1)); //Blue

    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT);
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
        //Display
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_13,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = GPIO_NUM_14,
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
        //SD Card
        spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_23,
                .miso_io_num = GPIO_NUM_19,
                .sclk_io_num = GPIO_NUM_18,
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
        //CN1 header, JST SH 1.25, GND / IO22 / IO21 / 3.3V
        uart::Configuration {
            .name = "UART1",
            .port = UART_NUM_1,
            .rxPin = GPIO_NUM_21,
            .txPin = GPIO_NUM_22,
            .rtsPin = GPIO_NUM_NC,
            .ctsPin = GPIO_NUM_NC,
            .rxBufferSize = 1024,
            .txBufferSize = 1024,
            .config = {
                .baud_rate = 9600,
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
