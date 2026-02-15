#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <tactility/drivers/i2c_controller.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/i2c/I2c.h>

using namespace tt::hal;

static constexpr auto* TAG = "Tab5";

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
    };
}

static error_t initPower(::Device* i2c_controller) {
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
    auto error = i2c_controller_write_register_array(i2c_controller, IO_EXPANDER1_ADDRESS, reg_data_io1_1, sizeof(reg_data_io1_1), pdMS_TO_TICKS(100));
    if (error != ERROR_NONE) {
        LOG_E(TAG, "IO expander 1 init failed in phase 1");
        return ERROR_UNDEFINED;
    }

    constexpr auto IO_EXPANDER2_ADDRESS = 0x44;
    error = i2c_controller_write_register_array(i2c_controller, IO_EXPANDER2_ADDRESS, reg_data_io2, sizeof(reg_data_io2), pdMS_TO_TICKS(100));
    if (error != ERROR_NONE) {
        LOG_E(TAG, "IO expander 2 init failed");
        return ERROR_UNDEFINED;
    }

    // The M5Stack code applies this, but it's not known why
    // TODO: Remove and test it extensively
    tt::kernel::delayTicks(10);

    error = i2c_controller_write_register_array(i2c_controller, IO_EXPANDER1_ADDRESS, reg_data_io1_2, sizeof(reg_data_io1_2), pdMS_TO_TICKS(100));
    if (error != ERROR_NONE) {
        LOG_E(TAG, "IO expander 1 init failed in phase 2");
        return ERROR_UNDEFINED;
    }

    return ERROR_NONE;
}

static error_t initSound(::Device* i2c_controller) {
    // Init data from M5Unified:
    // https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.cpp
    static constexpr uint8_t ES8388_I2C_ADDR = 0x10;
    static constexpr uint8_t ENABLED_BULK_DATA[] = {
        0, 0x80, // RESET/ CSM POWER ON
        0, 0x00,
        0, 0x00,
        0, 0x0E,
        1, 0x00,
        2, 0x0A, // CHIP POWER: power up all
        3, 0xFF, // ADC POWER: power down all
        4, 0x3C, // DAC POWER: power up and LOUT1/ROUT1/LOUT2/ROUT2 enable
        5, 0x00, // ChipLowPower1
        6, 0x00, // ChipLowPower2
        7, 0x7C, // VSEL
        8, 0x00, // set I2S slave mode
        // reg9-22 == adc
        23, 0x18, // I2S format (16bit)
        24, 0x00, // I2S MCLK ratio (128)
        25, 0x20, // DAC unmute
        26, 0x00, // LDACVOL 0x00~0xC0
        27, 0x00, // RDACVOL 0x00~0xC0
        28, 0x08, // enable digital click free power up and down
        29, 0x00,
        38, 0x00, // DAC CTRL16
        39, 0xB8, // LEFT Ch MIX
        42, 0xB8, // RIGHTCh MIX
        43, 0x08, // ADC and DAC separate
        45, 0x00, //   0x00=1.5k VREF analog output / 0x10=40kVREF analog output
        46, 0x21,
        47, 0x21,
        48, 0x21,
        49, 0x21
    };

    error_t error = i2c_controller_write_register_array(
        i2c_controller,
        ES8388_I2C_ADDR,
        ENABLED_BULK_DATA,
        sizeof(ENABLED_BULK_DATA),
        pdMS_TO_TICKS(1000)
    );
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8388: %s", error_to_string(error));
        return error;
    }

    constexpr auto IO_EXPANDER1_ADDRESS = 0x43;
    constexpr auto AMP_REGISTER = 0x05;
    // Note: to disable the amplifier, reset the bits
    error = i2c_controller_register8_set_bits(i2c_controller, IO_EXPANDER1_ADDRESS, AMP_REGISTER, 0b00000010, pdMS_TO_TICKS(100));
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable amplifier: %s", error_to_string(error));
        return error;
    }

    return ERROR_NONE;
}

static bool initBoot() {
    auto* i2c0 = device_find_by_name("i2c0");
    check(i2c0, "i2c0 not found");

    auto error = initPower(i2c0);
    if (error != ERROR_NONE) {
        return false;
    }

    error = initSound(i2c0);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8388");
    }

    return true;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
