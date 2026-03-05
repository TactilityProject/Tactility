#include <Tactility/Logger.h>
#include <driver/gpio.h>

static const auto LOGGER = tt::Logger("Paper S3");

constexpr gpio_num_t VBAT_PIN = GPIO_NUM_3;
constexpr gpio_num_t CHARGE_STATUS_PIN = GPIO_NUM_4;
constexpr gpio_num_t USB_DETECT_PIN = GPIO_NUM_5;

static bool powerOn() {
    if (gpio_reset_pin(CHARGE_STATUS_PIN) != ESP_OK) {
        LOGGER.error("Failed to reset CHARGE_STATUS_PIN");
        return false;
    }

    if (gpio_set_direction(CHARGE_STATUS_PIN, GPIO_MODE_INPUT) != ESP_OK) {
        LOGGER.error("Failed to set direction for CHARGE_STATUS_PIN");
        return false;
    }

    if (gpio_reset_pin(USB_DETECT_PIN) != ESP_OK) {
        LOGGER.error("Failed to reset USB_DETECT_PIN");
        return false;
    }

    if (gpio_set_direction(USB_DETECT_PIN, GPIO_MODE_INPUT) != ESP_OK) {
        LOGGER.error("Failed to set direction for USB_DETECT_PIN");
        return false;
    }

    // VBAT_PIN is used as ADC input; only reset it here to clear any previous
    // configuration. The ADC driver (ChargeFromAdcVoltage) configures it for ADC use.
    if (gpio_reset_pin(VBAT_PIN) != ESP_OK) {
        LOGGER.error("Failed to reset VBAT_PIN");
        return false;
    }
    return true;
}

bool initBoot() {
    LOGGER.info("Power on");
    if (!powerOn()) {
        LOGGER.error("Power on failed");
        return false;
    }

    return true;
}
