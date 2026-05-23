#include "devices/Display.h"
#include "devices/Power.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>
#include <drivers/py32ioexpander.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Tactility/hal/Configuration.h>
#include <Axp2101Power.h>
#include <AXP2101.h>

using namespace tt::hal;

static const auto* TAG = "StackChan";

// ---------------------------------------------------------------------------
// I2C addresses
// ---------------------------------------------------------------------------
static constexpr uint8_t AXP2101_ADDR = 0x34;
static constexpr uint8_t AW9523B_ADDR = 0x58;
static constexpr uint8_t AW88298_ADDR = 0x36;
static constexpr uint8_t ES7210_ADDR  = 0x40;

// ---------------------------------------------------------------------------
// AW9523B GPIO expander — same wiring as CoreS3
// ---------------------------------------------------------------------------
// P0 pins: 0=touch reset, 1=bus out enable, 2=AW88298 reset, 4=SD card, 5=USB OTG
// P1 pins: 0=cam reset, 1=LCD reset, 7=boost enable (SY7088)
static constexpr uint8_t AW9523B_CTL_REG   = 0x11; // P0 push-pull mode
static constexpr uint8_t AW9523B_P0_REG    = 0x02;
static constexpr uint8_t AW9523B_P1_REG    = 0x03;

static bool initGpioExpander(::Device* i2c) {
    // P0: touch, bus enable, AW88298 reset, SD card switch
    constexpr uint8_t p0 = (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 4U);
    // P1: LCD reset, boost enable
    constexpr uint8_t p1 = (1U << 1U) | (1U << 7U);

    // Set P0 to push-pull mode
    if (i2c_controller_register8_set(i2c, AW9523B_ADDR, AW9523B_CTL_REG, 0x10, pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        LOG_E(TAG, "AW9523B: Failed to set CTL");
        return false;
    }
    if (i2c_controller_register8_set(i2c, AW9523B_ADDR, AW9523B_P0_REG, p0, pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        LOG_E(TAG, "AW9523B: Failed to set P0");
        return false;
    }
    if (i2c_controller_register8_set(i2c, AW9523B_ADDR, AW9523B_P1_REG, p1, pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        LOG_E(TAG, "AW9523B: Failed to set P1");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// AXP2101 power management — same voltage rails as CoreS3
// ---------------------------------------------------------------------------
static bool initPowerControl(::Device* i2c) {
    // Source: https://github.com/m5stack/M5Unified/blob/b8cfec7fed046242da7f7b8024a4e92004a51ff7/src/utility/Power_Class.cpp#L64
    static constexpr uint8_t reg_data[] = {
        0x90U, 0xBFU,       // LDOS ON/OFF control 0 (backlight)
        0x92U, 18U - 5U,    // ALDO1 = 1.8V (AW88298)
        0x93U, 33U - 5U,    // ALDO2 = 3.3V (ES7210)
        0x94U, 33U - 5U,    // ALDO3 = 3.3V (camera)
        0x95U, 33U - 5U,    // ALDO4 = 3.3V (TF card)
        0x27U, 0x00U,       // PowerKey Hold=1sec / PowerOff=4sec
        0x69U, 0x11U,       // CHGLED setting
        0x10U, 0x30U,       // PMU common config
        0x30U, 0x0FU,       // ADC enabled
    };

    if (i2c_controller_write_register_array(i2c, AXP2101_ADDR, reg_data, sizeof(reg_data), pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        LOG_E(TAG, "AXP2101: Failed to set registers");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// AW88298 speaker amplifier
// Called once in initBoot() at 16kHz. AW88298 default state after hardware reset
// is sufficient for SfxEngine. Sequence mirrors M5Unified _speaker_enabled_cb_cores3().
// NOTE: 44100Hz (AudioPlayer/MusicPlayer) needs esp_codec_dev integration — see memory notes.
// ---------------------------------------------------------------------------
static bool initSpeaker(::Device* i2c, uint32_t sample_rate_hz) {
    // M5Unified rate table: (sample_rate + 1102) / 2205 steps, first entry >= result
    static constexpr uint8_t rate_tbl[] = { 4, 5, 6, 8, 10, 11, 15, 20, 22, 44 };
    size_t reg06_idx = 0;
    size_t rate = (sample_rate_hz + 1102) / 2205;
    while (rate > rate_tbl[reg06_idx] && ++reg06_idx < sizeof(rate_tbl)) {}
    if (reg06_idx >= sizeof(rate_tbl)) {
        reg06_idx = sizeof(rate_tbl) - 1; // clamp to max supported rate
    }
    // 0x14C0: M5Unified's upper byte for CoreS3, I2SBCK=0 (BCK 16*2)
    const uint16_t reg06 = static_cast<uint16_t>(0x14C0U | reg06_idx);

    // Hardware reset AW88298 via AW9523B P0 bit 2: pull LOW (reset), then HIGH (release).
    i2c_controller_register8_reset_bits(i2c, AW9523B_ADDR, AW9523B_P0_REG, 0b00000100, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_controller_register8_set_bits(i2c, AW9523B_ADDR, AW9523B_P0_REG, 0b00000100, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(50));

    // Exact sequence from M5Unified _speaker_enabled_cb_cores3() — no I2C software reset.
    struct { uint8_t reg; uint16_t val; } regs[] = {
        { 0x61, 0x0673 }, // boost mode disabled
        { 0x04, 0x4040 }, // I2SEN=1, AMPPD=0, PWDN=0
        { 0x05, 0x0008 }, // RMSE=0, HAGCE=0, HDCCE=0, HMUTE=0
        { 0x06, reg06  }, // I2S mode + sample rate
        { 0x0C, 0x3064 }, // volume -24dB
    };
    for (auto& r : regs) {
        if (i2c_controller_register16be_set(i2c, AW88298_ADDR, r.reg, r.val, pdMS_TO_TICKS(100)) != ERROR_NONE) {
            LOG_E(TAG, "AW88298: failed reg 0x%02X", r.reg);
            return false;
        }
    }

    LOG_I(TAG, "AW88298 initialized (%luHz, reg06=0x%04X)", (unsigned long)sample_rate_hz, reg06);
    return true;
}

// ---------------------------------------------------------------------------
// ES7210 microphone ADC
// Source: https://github.com/m5stack/M5Unified
// ---------------------------------------------------------------------------
static bool initMicrophone(::Device* i2c) {
    static constexpr uint8_t reg_data[] = {
        0x00, 0x41, // RESET_CTL
        0x01, 0x1F, // CLK_ON_OFF (initial)
        0x06, 0x00, // DIGITAL_PDN
        0x07, 0x20, // ADC_OSR
        0x08, 0x10, // MODE_CFG
        0x09, 0x30, // TCT0_CHPINI
        0x0A, 0x30, // TCT1_CHPINI
        0x20, 0x0A, // ADC34_HPF2
        0x21, 0x2A, // ADC34_HPF1
        0x22, 0x0A, // ADC12_HPF2
        0x23, 0x2A, // ADC12_HPF1
        0x02, 0xC1,
        0x04, 0x01,
        0x05, 0x00,
        0x11, 0x60,
        0x40, 0x42, // ANALOG_SYS
        0x41, 0x70, // MICBIAS12
        0x42, 0x70, // MICBIAS34
        0x43, 0x1B, // MIC1_GAIN
        0x44, 0x1B, // MIC2_GAIN
        0x45, 0x00, // MIC3_GAIN
        0x46, 0x00, // MIC4_GAIN
        0x47, 0x00, // MIC1_LP
        0x48, 0x00, // MIC2_LP
        0x49, 0x00, // MIC3_LP
        0x4A, 0x00, // MIC4_LP
        0x4B, 0x00, // MIC12_PDN
        0x4C, 0xFF, // MIC34_PDN
        0x01, 0x14, // CLK_ON_OFF (final)
    };

    if (i2c_controller_write_register_array(i2c, ES7210_ADDR, reg_data, sizeof(reg_data), pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        LOG_E(TAG, "ES7210: Failed to set registers");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// initBoot — called before device tree is started
// ---------------------------------------------------------------------------
static std::shared_ptr<Axp2101> axp2101;

bool initBoot() {
    auto* i2c = device_find_by_name("i2c_internal");
    if (i2c == nullptr) {
        LOG_E(TAG, "i2c_internal not found");
        return false;
    }
    // Boost enable via AXP2101 before GPIO expander init (same as CoreS3)
    // AW9523B P1 bit 7 = SY7088 boost enable — set after AXP2101 is configured
    if (!initPowerControl(i2c)) {
        LOG_E(TAG, "AXP2101 init failed");
        return false;
    }

    if (!initGpioExpander(i2c)) {
        LOG_E(TAG, "AW9523B init failed");
        return false;
    }

    // AW88298 default state after reset is sufficient for SfxEngine (16kHz).
    // Full 44100Hz support requires esp_codec_dev integration (see memory notes).
    if (!initSpeaker(i2c, 16000)) {
        LOG_W(TAG, "AW88298 init failed (non-fatal)");
    }

    if (!initMicrophone(i2c)) {
        LOG_W(TAG, "ES7210 init failed (non-fatal)");
    }

    // Boot LED pattern — confirms PY32IOExpander is working.
    // PY32 pin 0 = servo VM_EN (output), pin 13 = WS2812C data line.
    auto* py32 = device_find_by_name("py32");
    if (py32 != nullptr) {
        static constexpr uint8_t LED_COUNT = 12;
        static constexpr uint8_t COLORS[][3] = {
            { 255,   0,   0 }, // red
            {   0, 255,   0 }, // green
            {   0,   0, 255 }, // blue
        };
        py32_led_set_count(py32, LED_COUNT);
        for (auto& c : COLORS) {
            for (uint8_t i = 0; i < LED_COUNT; i++) {
                py32_led_set_color(py32, i, c[0], c[1], c[2]);
            }
            py32_led_refresh(py32);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        py32_led_disable(py32);
    } else {
        LOG_W(TAG, "py32 not found — LED boot pattern skipped");
    }

    // Keep Axp2101 C++ wrapper alive for Axp2101Power (backlight + battery)
    axp2101 = std::make_shared<Axp2101>(I2C_NUM_0);
    return true;
}

// ---------------------------------------------------------------------------
// Device list
// ---------------------------------------------------------------------------
static DeviceVector createDevices() {
    return {
        axp2101,
        std::make_shared<Axp2101Power>(axp2101),
        createPower(),
        createSdCard(),
        createDisplay(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
