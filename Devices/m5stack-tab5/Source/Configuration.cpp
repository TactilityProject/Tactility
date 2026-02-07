#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/i2c/I2c.h>

using namespace tt::hal;

static const auto LOGGER = tt::Logger("Tab5");

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
    };
}

static bool initBoot() {
    /*
     PI4IOE5V6408-1 (0x43)
     - Bit 0: RF internal/external switch
     - Bit 1: Speaker enable
     - Bit 2: External 5V bus enable
     - Bit 3: /
     - Bit 4: LCD reset
     - Bit 5: Touch reset
     - Bit 6: Camera reset
     - Bit 7: Headphone detect

     PI4IOE5V6408-2 (0x44)
     - Bit 0: C6 WLAN enable
     - Bit 1: /
     - Bit 2: /
     - Bit 3: USB-A 5V enable
     - Bit 4: Device power: PWROFF_PLUSE
     - Bit 5: IP2326: nCHG_QC_EN
     - Bit 6: IP2326: CHG_STAT_LED
     - Bit 7: IP2326: CHG_EN
    */

    // Init byte arrays adapted from https://github.com/m5stack/M5GFX/blob/03565ccc96cb0b73c8b157f5ec3fbde439b034ad/src/M5GFX.cpp
    static constexpr uint8_t reg_data_io1_1[] = {
        0x03, 0b01111111,   // PI4IO_REG_IO_DIR
        0x05, 0b01000110,   // PI4IO_REG_OUT_SET (bit4=LCD Reset, bit5=GT911 TouchReset -> LOW)
        0x07, 0b00000000,   // PI4IO_REG_OUT_H_IM
        0x0D, 0b01111111,   // PI4IO_REG_PULL_SEL
        0x0B, 0b01111111,   // PI4IO_REG_PULL_EN
    };

    static constexpr uint8_t reg_data_io1_2[] = {
        0x05, 0b01110110,   // PI4IO_REG_OUT_SET (bit4=LCD Reset, bit5=GT911 TouchReset -> HIGH)
    };

    static constexpr uint8_t reg_data_io2[] = {
        0x03, 0b10111001,   // PI4IO_REG_IO_DIR
        0x07, 0b00000110,   // PI4IO_REG_OUT_H_IM
        0x0D, 0b10111001,   // PI4IO_REG_PULL_SEL
        0x0B, 0b11111001,   // PI4IO_REG_PULL_EN
        0x09, 0b01000000,   // PI4IO_REG_IN_DEF_STA
        0x11, 0b10111111,   // PI4IO_REG_INT_MASK
        0x05, 0b10001001,   // PI4IO_REG_OUT_SET (enable WiFi, USB-A 5V and CHG_EN)
    };

    constexpr auto IO_EXPANDER1_ADDRESS = 0x43;
    if (!i2c::masterWriteRegisterArray(I2C_NUM_0, IO_EXPANDER1_ADDRESS, reg_data_io1_1, sizeof(reg_data_io1_1))) {
        LOGGER.error("IO expander 1 init failed in phase 1");
        return false;
    }

    constexpr auto IO_EXPANDER2_ADDRESS = 0x44;
    if (!i2c::masterWriteRegisterArray(I2C_NUM_0, IO_EXPANDER2_ADDRESS, reg_data_io2, sizeof(reg_data_io2))) {
        LOGGER.error("IO expander 2 init failed");
        return false;
    }

    // The M5Stack code applies this, but it's not known why
    // TODO: Remove and test it extensively
    tt::kernel::delayTicks(10);

    if (!i2c::masterWriteRegisterArray(I2C_NUM_0, IO_EXPANDER1_ADDRESS, reg_data_io1_2, sizeof(reg_data_io1_2))) {
        LOGGER.error("IO expander 1 init failed in phase 2");
        return false;
    }

    return true;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .spi = {
        // SDCard
        spi::Configuration {
            .device = SPI3_HOST,
            .dma = SPI_DMA_CH_AUTO,
            .config = {
                .mosi_io_num = GPIO_NUM_44,
                .miso_io_num = GPIO_NUM_39,
                .sclk_io_num = GPIO_NUM_43,
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
        }
    }
};
