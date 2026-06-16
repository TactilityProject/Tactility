#include "devices/Display.h"
#include "devices/Power.h"
#include "devices/Tab5Keyboard.h"

#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/i2c/I2c.h>
#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/settings/DisplaySettings.h>

using namespace tt::hal;

static constexpr auto* TAG = "Tab5";

static DeviceVector createDevices() {
    ::Device* i2c2 = device_find_by_name("i2c2");
    return {
        createPower(),
        createDisplay(),
        std::make_shared<Tab5Keyboard>(i2c2)
    };
}

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
constexpr auto GPIO_EXP0_PIN_RF_INTERNAL_EXTERNAL = 0;
constexpr auto GPIO_EXP0_PIN_SPEAKER_ENABLE = 1;
constexpr auto GPIO_EXP0_PIN_EXTERNAL_5V_BUS_ENABLE = 2;
constexpr auto GPIO_EXP0_PIN_LCD_RESET = 4;
constexpr auto GPIO_EXP0_PIN_TOUCH_RESET = 5;
constexpr auto GPIO_EXP0_PIN_CAMERA_RESET = 6;
constexpr auto GPIO_EXP0_PIN_HEADPHONE_DETECT = 7;

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
constexpr auto GPIO_EXP1_PIN_C6_WLAN_ENABLE = 0;
constexpr auto GPIO_EXP1_PIN_USB_A_5V_ENABLE = 3;
constexpr auto GPIO_EXP1_PIN_DEVICE_POWER = 4;
constexpr auto GPIO_EXP1_PIN_IP2326_NCHG_QC_EN = 5;
constexpr auto GPIO_EXP1_PIN_IP2326_CHG_STAT_LED = 6;
constexpr auto GPIO_EXP1_PIN_IP2326_CHG_EN = 7;

static void initExpander0(::Device* io_expander0) {
    auto* rf_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_RF_INTERNAL_EXTERNAL, GPIO_OWNER_GPIO);
    check(rf_pin);
    auto* speaker_enable_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_SPEAKER_ENABLE, GPIO_OWNER_GPIO);
    check(speaker_enable_pin);
    auto* external_5v_bus_enable_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_EXTERNAL_5V_BUS_ENABLE, GPIO_OWNER_GPIO);
    check(external_5v_bus_enable_pin);
    auto* lcd_reset_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_LCD_RESET, GPIO_OWNER_GPIO);
    check(lcd_reset_pin);
    auto* touch_reset_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_TOUCH_RESET, GPIO_OWNER_GPIO);
    check(touch_reset_pin);
    auto* camera_reset_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_CAMERA_RESET, GPIO_OWNER_GPIO);
    check(camera_reset_pin);
    auto* headphone_detect_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_HEADPHONE_DETECT, GPIO_OWNER_GPIO);
    check(headphone_detect_pin);

    gpio_descriptor_set_flags(rf_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(speaker_enable_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(external_5v_bus_enable_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(lcd_reset_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(touch_reset_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(camera_reset_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(headphone_detect_pin, GPIO_FLAG_DIRECTION_INPUT);

    gpio_descriptor_set_level(rf_pin, false);
    gpio_descriptor_set_level(speaker_enable_pin, false);
    gpio_descriptor_set_level(external_5v_bus_enable_pin, true);
    gpio_descriptor_set_level(lcd_reset_pin, false);
    gpio_descriptor_set_level(touch_reset_pin, false);
    gpio_descriptor_set_level(camera_reset_pin, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    // Enable touch and lcd, but not the camera
    gpio_descriptor_set_level(lcd_reset_pin, true);
    gpio_descriptor_set_level(touch_reset_pin, true);

    gpio_descriptor_release(rf_pin);
    gpio_descriptor_release(speaker_enable_pin);
    gpio_descriptor_release(external_5v_bus_enable_pin);
    gpio_descriptor_release(lcd_reset_pin);
    gpio_descriptor_release(touch_reset_pin);
    gpio_descriptor_release(camera_reset_pin);
    gpio_descriptor_release(headphone_detect_pin);
}

static void initExpander1(::Device* io_expander1) {

    auto* c6_wlan_enable_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_C6_WLAN_ENABLE, GPIO_OWNER_GPIO);
    check(c6_wlan_enable_pin);
    auto* usb_a_5v_enable_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_USB_A_5V_ENABLE, GPIO_OWNER_GPIO);
    check(usb_a_5v_enable_pin);
    auto* device_power_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_DEVICE_POWER, GPIO_OWNER_GPIO);
    check(device_power_pin);
    auto* ip2326_ncharge_qc_enable_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_IP2326_NCHG_QC_EN, GPIO_OWNER_GPIO);
    check(ip2326_ncharge_qc_enable_pin);
    auto* ip2326_charge_state_led_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_IP2326_CHG_STAT_LED, GPIO_OWNER_GPIO);
    check(ip2326_charge_state_led_pin);
    auto* ip2326_charge_enable_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_IP2326_CHG_EN, GPIO_OWNER_GPIO);
    check(ip2326_charge_enable_pin);

    gpio_descriptor_set_flags(c6_wlan_enable_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(usb_a_5v_enable_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(device_power_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(ip2326_ncharge_qc_enable_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(ip2326_charge_state_led_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(ip2326_charge_enable_pin, GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP);

    gpio_descriptor_set_level(c6_wlan_enable_pin, true);
    gpio_descriptor_set_level(usb_a_5v_enable_pin, true);
    gpio_descriptor_set_level(device_power_pin, false);
    gpio_descriptor_set_level(ip2326_ncharge_qc_enable_pin, false);
    gpio_descriptor_set_level(ip2326_charge_state_led_pin, false);

    gpio_descriptor_release(c6_wlan_enable_pin);
    gpio_descriptor_release(usb_a_5v_enable_pin);
    gpio_descriptor_release(device_power_pin);
    gpio_descriptor_release(ip2326_ncharge_qc_enable_pin);
    gpio_descriptor_release(ip2326_charge_state_led_pin);
    gpio_descriptor_release(ip2326_charge_enable_pin);
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

    auto* speaker_enable_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_SPEAKER_ENABLE, GPIO_OWNER_GPIO);
    check(speaker_enable_pin, "Failed to acquire speaker enable pin");
    error = gpio_descriptor_set_level(speaker_enable_pin, true);
    gpio_descriptor_release(speaker_enable_pin);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable amplifier: %s", error_to_string(error));
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t initMicrophone(::Device* i2c_controller) {
    // ES7210 quad-channel microphone ADC at 0x40.
    // Register sequence from M5Unified (M5Unified.cpp, _microphone_enabled_cb_tab5).
    // Configures 4-slot TDM output at 48kHz/16-bit with MIC1+MIC2 active and MICBIAS enabled.
    static constexpr uint8_t ES7210_I2C_ADDR = 0x40;
    static constexpr uint8_t INIT_DATA[] = {
        0x00, 0xFF, // RESET_CTL: full reset
        0x00, 0x41, // RESET_CTL: release reset, keep CSM active
        0x01, 0x1F, // CLK_ON_OFF: enable all clocks
        0x06, 0x00, // DIGITAL_PDN: power up all digital blocks
        0x07, 0x20, // ADC_OSR: OSR=256
        0x08, 0x10, // MODE_CFG: I2S slave, TDM mode
        0x09, 0x30, // TCT0_CHPINI: chopper init period
        0x0A, 0x30, // TCT1_CHPINI
        0x20, 0x0A, // ADC34_HPF2
        0x21, 0x2A, // ADC34_HPF1
        0x22, 0x0A, // ADC12_HPF2
        0x23, 0x2A, // ADC12_HPF1
        0x02, 0xC1, // CLK_CTRL: MCLK from I2S, PLL off
        0x04, 0x01, // SDPOUT_CTL1: TDM output enable
        0x05, 0x00, // SDPOUT_CTL0
        0x11, 0x60, // DBIAS: adjust reference voltage for P4
        0x40, 0x42, // ANALOG_SYS: enable analog supply
        0x41, 0x70, // MICBIAS12: enable MICBIAS for MIC1+MIC2
        0x42, 0x70, // MICBIAS34: enable MICBIAS for MIC3+MIC4
        0x43, 0x1B, // MIC1_GAIN: +30 dB
        0x44, 0x1B, // MIC2_GAIN: +30 dB
        0x45, 0x00, // MIC3_GAIN: AEC ref, no gain
        0x46, 0x00, // MIC4_GAIN: AEC ref, no gain
        0x47, 0x00, // MIC1_LP
        0x48, 0x00, // MIC2_LP
        0x49, 0x00, // MIC3_LP
        0x4A, 0x00, // MIC4_LP
        0x4B, 0x00, // MIC12_PDN: power up MIC1+MIC2
        0x4C, 0xFF, // MIC34_PDN: keep MIC3+MIC4 in power-down (AEC ref not needed)
        0x01, 0x14, // CLK_ON_OFF: final clock config
    };

    error_t error = i2c_controller_write_register_array(
        i2c_controller,
        ES7210_I2C_ADDR,
        INIT_DATA,
        sizeof(INIT_DATA),
        pdMS_TO_TICKS(1000)
    );
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to init ES7210: %s", error_to_string(error));
    }
    return error;
}

static bool initBoot() {
    auto* i2c0 = device_find_by_name("i2c0");
    check(i2c0, "i2c0 not found");

    auto* io_expander0 = device_find_by_name("io_expander0");
    check(io_expander0, "io_expander0 not found");
    auto* io_expander1 = device_find_by_name("io_expander1");
    check(io_expander1, "io_expander1 not found");

    initExpander0(io_expander0);
    initExpander1(io_expander1);

    error_t error = initSound(i2c0, io_expander0);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable ES8388");
    }

    error = initMicrophone(i2c0);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to init ES7210");
    }

    return true;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
