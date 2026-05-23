#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/Power.h"
#include <driver/gpio.h>

#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <drivers/m5pm1.h>

#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static constexpr auto* TAG = "StickS3";

static constexpr uint8_t ES8311_I2C_ADDR = 0x18;

static error_t initSound(::Device* i2c_controller) {
    // Init data from M5Unified:
    // https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.cpp#L454
    static constexpr uint8_t ENABLED_BULK_DATA[] = {
        0x00, 0x80,  // 0x00 RESET/  CSM POWER ON
        0x01, 0x3F,  // 0x01 CLOCK_MANAGER/ use MCLK pin (external), all clocks on
        0x02, 0x00,  // 0x02 CLOCK_MANAGER/ pre_div=1 pre_multi=1 (256x MCLK from ESP32 is correct ratio)
        0x0D, 0x01,  // 0x0D SYSTEM/ Power up analog circuitry
        0x12, 0x00,  // 0x12 SYSTEM/ power-up DAC - NOT default
        0x13, 0x10,  // 0x13 SYSTEM/ Enable output to HP drive - NOT default
        0x32, 0xBF,  // 0x32 DAC/ DAC volume (0xBF == ±0 dB )
        0x37, 0x08,  // 0x37 DAC/ Bypass DAC equalizer - NOT default
    };

    error_t error = i2c_controller_write_register_array(
        i2c_controller,
        ES8311_I2C_ADDR,
        ENABLED_BULK_DATA,
        sizeof(ENABLED_BULK_DATA),
        pdMS_TO_TICKS(1000)
    );
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8311: %s", error_to_string(error));
        return error;
    }

    // Enable speaker amp via M5PM1 driver
    auto* m5pm1 = device_find_by_name("m5pm1");
    if (m5pm1 != nullptr) {
        m5pm1_set_speaker_enable(m5pm1, true);
    } else {
        LOG_W(TAG, "m5pm1 not found — speaker amp not enabled");
    }

    return ERROR_NONE;
}

static error_t initMicrophone(::Device* i2c_controller) {
    // Init data from M5Unified:
    // https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.cpp#L842
    static constexpr uint8_t ENABLED_BULK_DATA[] = {
        0x00, 0x80,  // 0x00 RESET/  CSM POWER ON
        0x01, 0x3F,  // 0x01 CLOCK_MANAGER/ use MCLK pin (external), all clocks on
        0x02, 0x00,  // 0x02 CLOCK_MANAGER/ pre_div=1 pre_multi=1 (256x MCLK from ESP32 is correct ratio)
        0x0D, 0x01,  // 0x0D SYSTEM/ Power up analog circuitry
        0x0E, 0x02,  // 0x0E SYSTEM/ : Enable analog PGA, enable ADC modulator
        0x14, 0x10,  // ES8311_ADC_REG14 : select Mic1p-Mic1n / PGA GAIN (minimum)
        0x17, 0xFF,  // ES8311_ADC_REG17 : ADC_VOLUME (MAXGAIN) // (0xBF == ± 0 dB )
        0x1C, 0x6A,  // ES8311_ADC_REG1C : ADC Equalizer bypass, cancel DC offset in digital domain
    };

    error_t error = i2c_controller_write_register_array(
        i2c_controller,
        ES8311_I2C_ADDR,
        ENABLED_BULK_DATA,
        sizeof(ENABLED_BULK_DATA),
        pdMS_TO_TICKS(1000)
    );
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8311: %s", error_to_string(error));
        return error;
    }

    return ERROR_NONE;
}

bool initBoot() {
    auto* i2c_internal = device_find_by_name("i2c_internal");
    check(i2c_internal, "i2c_internal not found");

    error_t error = initSound(i2c_internal);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8311 speaker");
    }

    error = initMicrophone(i2c_internal);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8311 microphone");
    }
    return driver::pwmbacklight::init(GPIO_NUM_38, 512);
}

static DeviceVector createDevices() {
    return {
        createPower(),
        ButtonControl::createTwoButtonControl(11, 12), // top button, side button
        createDisplay(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
