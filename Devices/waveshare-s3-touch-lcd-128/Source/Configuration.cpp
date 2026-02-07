#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <PwmBacklight.h>

using namespace tt::hal;

constexpr auto* TAG = "Waveshare";

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

static bool initBoot() {
    return driver::pwmbacklight::init(GPIO_NUM_2, 256);
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiScale = UiScale::Smallest,
    .createDevices = createDevices,
    .spi {
        // Display
        spi::Configuration {
            .device = SPI2_HOST,
            .dma = SPI_DMA_DISABLED,
            .config = {
                .mosi_io_num = GPIO_NUM_11,
                .miso_io_num = GPIO_NUM_12,
                .sclk_io_num = GPIO_NUM_10,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .data4_io_num = GPIO_NUM_NC,
                .data5_io_num = GPIO_NUM_NC,
                .data6_io_num = GPIO_NUM_NC,
                .data7_io_num = GPIO_NUM_NC,
                .data_io_default_level = false,
                .max_transfer_sz = ((240 * (240 / 10)) * LV_COLOR_DEPTH / 8),
                .flags = 0,
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
                .intr_flags = 0
            },
            .initMode = spi::InitMode::ByTactility,
            .isMutable = false,
            .lock = tt::lvgl::getSyncLock() // esp_lvgl_port owns the lock for the display
        },
        // SD card available via external sd card module and uses VSYS (5V) / GND / IO15 / IO16 / IO17 / IO18 pins.
        // Common micro sd card module you'd find on aliexpress with voltage regulator onboard. Others may work.
        // JST SH 1.0 Header, GND / VSYS (5V) / RESET / BOOT / GND / 3.3V / IO15 / IO16 / IO17 / IO18 / IO21 / IO33
         spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_16,
                .miso_io_num = GPIO_NUM_15,
                .sclk_io_num = GPIO_NUM_17,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .data4_io_num = GPIO_NUM_NC,
                .data5_io_num = GPIO_NUM_NC,
                .data6_io_num = GPIO_NUM_NC,
                .data7_io_num = GPIO_NUM_NC,
                .data_io_default_level = false,
                .max_transfer_sz = 32768,
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
