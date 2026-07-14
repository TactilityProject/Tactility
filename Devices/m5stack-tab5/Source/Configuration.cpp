#include "devices/Display.h"
#include "devices/Power.h"
#include "devices/Tab5Keyboard.h"

#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>

#include <Tactility/hal/Configuration.h>

#include <esp_clock_output.h>

using namespace tt::hal;

static constexpr auto* TAG = "Tab5";

static DeviceVector createDevices() {
    auto* i2c2 = device_find_by_name("i2c2");
    check(i2c2, "i2c2 not found");
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

// Audio codec register programming is owned by the es8388-module/es7210-module drivers
// (registered as AUDIO_CODEC_TYPE devices, see m5stack,tab5.dts). This board still needs
// to drive the external speaker amplifier's enable line via io_expander0, which is board
// wiring glue rather than codec configuration, so it stays here.
static error_t enableSpeakerAmplifier(::Device* io_expander0) {
    auto* speaker_enable_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_SPEAKER_ENABLE, GPIO_OWNER_GPIO);
    check(speaker_enable_pin, "Failed to acquire speaker enable pin");
    error_t error = gpio_descriptor_set_level(speaker_enable_pin, true);
    gpio_descriptor_release(speaker_enable_pin);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable amplifier: %s", error_to_string(error));
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}


static esp_clock_output_mapping_handle_t camera_osc_handle = nullptr;

static void initCameraOsc() {
    if (camera_osc_handle != nullptr) {
        return;
    }

    // 24 MHz clock on GPIO 36 for the SC2356 MIPI CSI sensor, required before esp_video_init.
    // Uses SPLL (480 MHz) via esp_clock_output with divider 20 = 24 MHz exactly.
    // This avoids any LEDC clock source conflict with the display backlight.
    if (esp_clock_output_start(CLKOUT_SIG_SPLL, GPIO_NUM_36, &camera_osc_handle) != ESP_OK) {
        LOG_E(TAG, "Camera OSC clock output start failed");
        return;
    }
    if (esp_clock_output_set_divider(camera_osc_handle, 20) != ESP_OK) {
        LOG_E(TAG, "Camera OSC clock divider set failed");
        esp_clock_output_stop(camera_osc_handle);
        camera_osc_handle = nullptr;
        return;
    }
    LOG_I(TAG, "Camera OSC 24MHz started on GPIO 36 (SPLL/20)");
}

static bool initBoot() {
    auto* io_expander0 = device_find_by_name("io_expander0");
    check(io_expander0, "io_expander0 not found");
    auto* io_expander1 = device_find_by_name("io_expander1");
    check(io_expander1, "io_expander1 not found");

    initCameraOsc();
    initExpander0(io_expander0);
    initExpander1(io_expander1);

    error_t error = enableSpeakerAmplifier(io_expander0);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable speaker amplifier");
    }

    return true;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
