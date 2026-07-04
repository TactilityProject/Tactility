#include "UnPhoneFeatures.h"

#include <Tactility/app/App.h>
#include <Tactility/kernel/Kernel.h>

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_io_expander.h>
#include <esp_sleep.h>
#include <tactility/log.h>

constexpr auto* TAG = "unPhoneFeatures";

namespace pin {
    static const gpio_num_t BUTTON1 = GPIO_NUM_45; // left button
    static const gpio_num_t BUTTON2 = GPIO_NUM_0; // middle button
    static const gpio_num_t BUTTON3 = GPIO_NUM_21; // right button
    static const gpio_num_t IR_LEDS = GPIO_NUM_12;
    static const gpio_num_t LED_RED = GPIO_NUM_13;
    static const gpio_num_t POWER_SWITCH = GPIO_NUM_18;
} // namespace pin

namespace expanderpin {
    static const esp_io_expander_pin_num_t BACKLIGHT = IO_EXPANDER_PIN_NUM_2;
    static const esp_io_expander_pin_num_t EXPANDER_POWER = IO_EXPANDER_PIN_NUM_0; // enable exp brd if high
    static const esp_io_expander_pin_num_t LED_GREEN = IO_EXPANDER_PIN_NUM_9;
    static const esp_io_expander_pin_num_t LED_BLUE = IO_EXPANDER_PIN_NUM_13;
    static const esp_io_expander_pin_num_t USB_VSENSE = IO_EXPANDER_PIN_NUM_14;
    static const esp_io_expander_pin_num_t VIBE = IO_EXPANDER_PIN_NUM_7;
} // namespace expanderpin

// TODO: Make part of a new type of UnPhoneFeatures data struct that holds all the thread-related data
QueueHandle_t interruptQueue;

static void IRAM_ATTR navButtonInterruptHandler(void* args) {
    int pinNumber = (int)args;
    xQueueSendFromISR(interruptQueue, &pinNumber, NULL);
}

static int32_t buttonHandlingThreadMain(const bool* interrupted) {
    int pinNumber;
    while (!*interrupted) {
        if (xQueueReceive(interruptQueue, &pinNumber, portMAX_DELAY)) {
            // The buttons might generate more than 1 click because of how they are built
            LOG_I(TAG, "Pressed button %d", pinNumber);
            if (pinNumber == pin::BUTTON1) {
                tt::app::stop();
            }

            // Debounce all events for a short period of time
            // This is easier than keeping track when each button was last pressed
            tt::kernel::delayMillis(50);
            xQueueReset(interruptQueue);
            tt::kernel::delayMillis(50);
            xQueueReset(interruptQueue);
        }
    }
    return 0;
}

UnPhoneFeatures::~UnPhoneFeatures() {
    if (buttonHandlingThread.getState() != tt::Thread::State::Stopped) {
        buttonHandlingThreadInterruptRequest = true;
        buttonHandlingThread.join();
    }
}

bool UnPhoneFeatures::initPowerSwitch() {
    gpio_config_t config = {
        .pin_bit_mask = BIT64(pin::POWER_SWITCH),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };

    if (gpio_config(&config) != ESP_OK) {
        LOG_E(TAG, "Power pin init failed");
        return false;
    }

    if (rtc_gpio_pullup_en(pin::POWER_SWITCH) == ESP_OK &&
        rtc_gpio_pulldown_en(pin::POWER_SWITCH) == ESP_OK) {
        return true;
    } else {
        LOG_E(TAG, "Failed to set RTC for power switch");
        return false;
    }
}

bool UnPhoneFeatures::initNavButtons() {
    if (!initGpioExpander()) {
        LOG_E(TAG, "GPIO expander init failed");
        return false;
    }

    interruptQueue = xQueueCreate(4, sizeof(int));

    buttonHandlingThread.setName("unphone_buttons");
    buttonHandlingThread.setPriority(tt::Thread::Priority::High);
    buttonHandlingThread.setStackSize(3072);
    buttonHandlingThread.setMainFunction(
        [this] {
            return buttonHandlingThreadMain(&this->buttonHandlingThreadInterruptRequest);
        }
    );
    buttonHandlingThread.start();

    uint64_t pin_mask =
        BIT64(pin::BUTTON1) |
        BIT64(pin::BUTTON2) |
        BIT64(pin::BUTTON3);

    gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        /**
         * We have to listen to the button release (= positive signal).
         * If we listen to button press, the buttons might create more than 1 signal
         * when they are continuously pressed.
         */
        .intr_type = GPIO_INTR_POSEDGE,
    };

    if (gpio_config(&config) != ESP_OK) {
        LOG_E(TAG, "Nav button pin init failed");
        return false;
    }

    if (
        gpio_install_isr_service(0) != ESP_OK ||
        gpio_isr_handler_add(pin::BUTTON1, navButtonInterruptHandler, reinterpret_cast<void*>(pin::BUTTON1)) != ESP_OK ||
        gpio_isr_handler_add(pin::BUTTON2, navButtonInterruptHandler, reinterpret_cast<void*>(pin::BUTTON2)) != ESP_OK ||
        gpio_isr_handler_add(pin::BUTTON3, navButtonInterruptHandler, reinterpret_cast<void*>(pin::BUTTON3)) != ESP_OK
    ) {
        LOG_E(TAG, "Nav buttons ISR init failed");
        return false;
    }

    return true;
}

bool UnPhoneFeatures::initOutputPins() {
    uint64_t output_pin_mask =
        BIT64(pin::IR_LEDS) |
        BIT64(pin::LED_RED);

    gpio_config_t config = {
        .pin_bit_mask = output_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&config) != ESP_OK) {
        LOG_E(TAG, "Output pin init failed");
        return false;
    }

    return true;
}

bool UnPhoneFeatures::initGpioExpander() {
    // ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_110 corresponds with 0x26 from the docs at
    // https://gitlab.com/hamishcunningham/unphonelibrary/-/blob/main/unPhone.h?ref_type=heads#L206
    if (esp_io_expander_new_i2c_tca95xx_16bit(I2C_NUM_0, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_110, &ioExpander) != ESP_OK) {
        LOG_E(TAG, "IO expander init failed");
        return false;
    }
    assert(ioExpander != nullptr);

    // Output pins

    /**
     * Important:
     * If you clear the pins too late, the display or vibration motor might briefly turn on.
     */

    esp_io_expander_set_dir(ioExpander, expanderpin::BACKLIGHT, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(ioExpander, expanderpin::BACKLIGHT, 0);

    esp_io_expander_set_dir(ioExpander, expanderpin::EXPANDER_POWER, IO_EXPANDER_OUTPUT);

    esp_io_expander_set_dir(ioExpander, expanderpin::LED_GREEN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(ioExpander, expanderpin::LED_GREEN, 0);

    esp_io_expander_set_dir(ioExpander, expanderpin::LED_BLUE, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(ioExpander, expanderpin::LED_BLUE, 0);

    esp_io_expander_set_dir(ioExpander, expanderpin::VIBE, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(ioExpander, expanderpin::VIBE, 0);

    // Input pins

    esp_io_expander_set_dir(ioExpander, expanderpin::USB_VSENSE, IO_EXPANDER_INPUT);

    return true;
}

bool UnPhoneFeatures::init() {
    LOG_I(TAG, "init");

    if (!initGpioExpander()) {
        LOG_E(TAG, "GPIO expander init failed");
        return false;
    }

    if (!initNavButtons()) {
        LOG_E(TAG, "Input pin init failed");
        return false;
    }

    if (!initOutputPins()) {
        LOG_E(TAG, "Output pin init failed");
        return false;
    }

    if (!initPowerSwitch()) {
        LOG_E(TAG, "Power button init failed");
        return false;
    }

    return true;
}

void UnPhoneFeatures::printInfo() const {
    esp_io_expander_print_state(ioExpander);
    batteryManagement->printInfo();
    bool backlight_power;
    const char* backlight_power_state = getBacklightPower(backlight_power) && backlight_power ? "on" : "off";
    LOG_I(TAG, "Backlight: %s", backlight_power_state);
}

bool UnPhoneFeatures::setRgbLed(bool red, bool green, bool blue) const {
    assert(ioExpander != nullptr);
    return gpio_set_level(pin::LED_RED, red ? 1U : 0U) == ESP_OK &&
        esp_io_expander_set_level(ioExpander, expanderpin::LED_GREEN, green ? 1U : 0U) == ESP_OK &&
        esp_io_expander_set_level(ioExpander, expanderpin::LED_BLUE, blue ? 1U : 0U) == ESP_OK;
}

bool UnPhoneFeatures::setBacklightPower(bool on) const {
    assert(ioExpander != nullptr);
    return esp_io_expander_set_level(ioExpander, expanderpin::BACKLIGHT, on ? 1U : 0U) == ESP_OK;
}

bool UnPhoneFeatures::getBacklightPower(bool& on) const {
    assert(ioExpander != nullptr);
    uint32_t level_mask;
    if (esp_io_expander_get_level(ioExpander, expanderpin::BACKLIGHT, &level_mask) == ESP_OK) {
        on = level_mask != 0U;
        return true;
    } else {
        return false;
    }
}

bool UnPhoneFeatures::setIrPower(bool on) const {
    assert(ioExpander != nullptr);
    return gpio_set_level(pin::IR_LEDS, on ? 1U : 0U) == ESP_OK;
}

bool UnPhoneFeatures::setVibePower(bool on) const {
    assert(ioExpander != nullptr);
    return esp_io_expander_set_level(ioExpander, expanderpin::VIBE, on ? 1U : 0U) == ESP_OK;
}

bool UnPhoneFeatures::setExpanderPower(bool on) const {
    assert(ioExpander != nullptr);
    return esp_io_expander_set_level(ioExpander, expanderpin::EXPANDER_POWER, on ? 1U : 0U) == ESP_OK;
}

bool UnPhoneFeatures::isPowerSwitchOn() const {
    return gpio_get_level(pin::POWER_SWITCH) > 0;
}

void UnPhoneFeatures::turnPeripheralsOff() const {
    setExpanderPower(false);
    setBacklightPower(false);
    setIrPower(false);
    setRgbLed(false, false, false);
    setVibePower(false);
}

bool UnPhoneFeatures::setShipping(bool on) const {
    if (on) {
        LOG_W(TAG, "setShipping: on");
        batteryManagement->setWatchDogTimer(Bq24295::WatchDogTimer::Disabled);
        batteryManagement->setBatFetOn(false);
    } else {
        LOG_W(TAG, "setShipping: off");
        batteryManagement->setWatchDogTimer(Bq24295::WatchDogTimer::Enabled40s);
        batteryManagement->setBatFetOn(true);
    }
    return true;
}

void UnPhoneFeatures::wakeOnPowerSwitch() const {
    esp_sleep_enable_ext0_wakeup(pin::POWER_SWITCH, 1);
}

bool UnPhoneFeatures::isUsbPowerConnected() const {
    return batteryManagement->isUsbPowerConnected();
}
