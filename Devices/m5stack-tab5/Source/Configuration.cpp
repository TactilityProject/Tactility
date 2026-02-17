#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <tactility/drivers/i2c_controller.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/i2c/I2c.h>
#include <drivers/pi4ioe5v6408.h>

using namespace tt::hal;

static constexpr auto* TAG = "Tab5";

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
    };
}

static error_t initPower(::Device* io_expander0, ::Device* io_expander1) {
    constexpr TickType_t i2c_timeout = pdMS_TO_TICKS(10);

    /*
     PI4IOE5V6408-0 (0x43)
     - Bit 0: RF internal/external switch
     - Bit 1: Speaker enable
     - Bit 2: External 5V bus enable
     - Bit 3: /
     - Bit 4: LCD reset
     - Bit 5: Touch reset
     - Bit 6: Camera reset
     - Bit 7: Headphone detect
     */

    check(pi4ioe5v6408_set_direction(io_expander0, 0b01111111, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_output_level(io_expander0, 0b01000110, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_output_high_impedance(io_expander0, 0b00000000, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_pull_select(io_expander0, 0b01111111, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_pull_enable(io_expander0, 0b01111111, i2c_timeout) == ERROR_NONE);
    vTaskDelay(pdMS_TO_TICKS(10));
    check(pi4ioe5v6408_set_output_level(io_expander0, 0b01110110, i2c_timeout) == ERROR_NONE);

    /*
     PI4IOE5V6408-1 (0x44)
     - Bit 0: C6 WLAN enable
     - Bit 1: /
     - Bit 2: /
     - Bit 3: USB-A 5V enable
     - Bit 4: Device power: PWROFF_PLUSE
     - Bit 5: IP2326: nCHG_QC_EN
     - Bit 6: IP2326: CHG_STAT_LED
     - Bit 7: IP2326: CHG_EN
    */

    check(pi4ioe5v6408_set_direction(io_expander1, 0b10111001, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_output_high_impedance(io_expander1, 0b00000110, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_pull_select(io_expander1, 0b10111001, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_pull_enable(io_expander1, 0b11111001, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_input_default_level(io_expander1, 0b01000000, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_interrupt_mask(io_expander1, 0b10111111, i2c_timeout) == ERROR_NONE);
    check(pi4ioe5v6408_set_output_level(io_expander1, 0b10001001, i2c_timeout) == ERROR_NONE);

    return ERROR_NONE;
}

static error_t initSound(::Device* i2c_controller, ::Device* io_expander0 = nullptr) {
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

    uint8_t output_level = 0;
    if (pi4ioe5v6408_get_output_level(io_expander0, &output_level, pdMS_TO_TICKS(100)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to read power level: %s", error_to_string(error));
        return ERROR_RESOURCE;
    }

    if (pi4ioe5v6408_set_output_level(io_expander0, output_level | 0b00000010, pdMS_TO_TICKS(100)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable amplifier: %s", error_to_string(error));
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static bool initBoot() {
    auto* i2c0 = device_find_by_name("i2c0");
    check(i2c0, "i2c0 not found");

    auto* io_expander0 = device_find_by_name("io_expander0");
    auto* io_expander1 = device_find_by_name("io_expander1");
    check(i2c0, "i2c0 not found");

    initPower(io_expander0, io_expander1);

    error_t error = initSound(i2c0, io_expander0);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8388");
    }

    return true;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
