#include "ButtonControl.h"

#include <Tactility/app/App.h>
#include <Tactility/Logger.h>

#include <esp_lvgl_port.h>

static const auto LOGGER = tt::Logger("ButtonControl");

ButtonControl::ButtonControl(const std::vector<PinConfiguration>& pinConfigurations)
    : buttonQueue(20, sizeof(ButtonEvent)),
      pinConfigurations(pinConfigurations) {

    pinStates.resize(pinConfigurations.size());

    // Build isrArgs with one entry per unique physical pin, then configure GPIO.
    isrArgs.reserve(pinConfigurations.size());
    for (size_t i = 0; i < pinConfigurations.size(); i++) {
        const auto pin = static_cast<gpio_num_t>(pinConfigurations[i].pin);

        // Skip if this physical pin was already seen.
        bool seen = false;
        for (const auto& arg : isrArgs) {
            if (arg.pin == pin) { seen = true; break; }
        }
        if (seen) continue;

        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            LOGGER.error("Failed to configure GPIO {}: {}", static_cast<int>(pin), esp_err_to_name(err));
            continue;
        }

        // isrArgs is reserved upfront; push_back will not reallocate, keeping addresses stable
        // for gpio_isr_handler_add() called later in startThread().
        isrArgs.push_back({ .self = this, .pin = pin });
    }
}

ButtonControl::~ButtonControl() {
    if (driverThread != nullptr && driverThread->getState() != tt::Thread::State::Stopped) {
        stopThread();
    }
}

void ButtonControl::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    // Defaults
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    auto* self = static_cast<ButtonControl*>(lv_indev_get_driver_data(indev));

    if (self->mutex.lock(100)) {

        for (int i = 0; i < self->pinConfigurations.size(); i++) {
            const auto& config = self->pinConfigurations[i];
            std::vector<PinState>::reference state = self->pinStates[i];
            const bool trigger = (config.event == Event::ShortPress && state.triggerShortPress) ||
                (config.event == Event::LongPress && state.triggerLongPress);
            state.triggerShortPress = false;
            state.triggerLongPress = false;
            if (trigger) {
                switch (config.action) {
                    case Action::UiSelectNext:
                        data->enc_diff = 1;
                        break;
                    case Action::UiSelectPrevious:
                        data->enc_diff = -1;
                        break;
                    case Action::UiPressSelected:
                        data->state = LV_INDEV_STATE_PRESSED;
                        break;
                    case Action::AppClose:
                        tt::app::stop();
                        break;
                }
            }
        }
        self->mutex.unlock();
    }
}

void ButtonControl::updatePin(std::vector<PinConfiguration>::const_reference configuration, std::vector<PinState>::reference state, bool pressed) {
    auto now = tt::kernel::getMillis();

    // Software debounce: ignore edges within 20ms of the last state change.
    if ((now - state.lastChangeTime) < 20) {
        return;
    }
    state.lastChangeTime = now;

    if (pressed) {
        state.pressStartTime = now;
        state.pressState = true;
    } else { // released
        if (state.pressState) {
            auto time_passed = now - state.pressStartTime;
            if (time_passed < 500) {
                LOGGER.info("Short press ({}ms)", time_passed);
                state.triggerShortPress = true;
            } else {
                LOGGER.info("Long press ({}ms)", time_passed);
                state.triggerLongPress = true;
            }
            state.pressState = false;
        }
    }
}

void IRAM_ATTR ButtonControl::gpioIsrHandler(void* arg) {
    auto* isrArg = static_cast<IsrArg*>(arg);
    ButtonEvent event {
        .pin = isrArg->pin,
        .pressed = gpio_get_level(isrArg->pin) == 0, // active-low: LOW = pressed
    };
    // tt::MessageQueue::put() is ISR-safe with timeout=0: it detects ISR context via
    // xPortInIsrContext() and uses xQueueSendFromISR() + portYIELD_FROM_ISR() internally.
    isrArg->self->buttonQueue.put(&event, 0);
}

void ButtonControl::driverThreadMain() {
    ButtonEvent event;
    while (buttonQueue.get(&event, portMAX_DELAY)) {
        if (event.pin == GPIO_NUM_NC) {
            break; // shutdown sentinel
        }
        LOGGER.info("Pin {} {}", static_cast<int>(event.pin), event.pressed ? "down" : "up");
        if (mutex.lock(portMAX_DELAY)) {
            // Update ALL PinConfiguration entries that share this physical pin.
            for (size_t i = 0; i < pinConfigurations.size(); i++) {
                if (static_cast<gpio_num_t>(pinConfigurations[i].pin) == event.pin) {
                    updatePin(pinConfigurations[i], pinStates[i], event.pressed);
                }
            }
            mutex.unlock();
        }
    }
}

bool ButtonControl::startThread() {
    LOGGER.info("Start");

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGGER.error("Failed to install GPIO ISR service: {}", esp_err_to_name(err));
        return false;
    }

    // isrArgs has one entry per unique physical pin — no duplicate registrations.
    // Addresses are stable: vector was reserved in constructor and is not modified after that.
    int handlersAdded = 0;
    for (auto& arg : isrArgs) {
        err = gpio_isr_handler_add(arg.pin, gpioIsrHandler, &arg);
        if (err != ESP_OK) {
            LOGGER.error("Failed to add ISR for GPIO {}: {}", static_cast<int>(arg.pin), esp_err_to_name(err));
            for (int i = 0; i < handlersAdded; i++) {
                gpio_isr_handler_remove(isrArgs[i].pin);
            }
            return false;
        }
        handlersAdded++;
    }

    driverThread = std::make_shared<tt::Thread>("ButtonControl", 4096, [this] {
        driverThreadMain();
        return 0;
    });

    driverThread->start();
    return true;
}

void ButtonControl::stopThread() {
    LOGGER.info("Stop");

    for (const auto& arg : isrArgs) {
        gpio_isr_handler_remove(arg.pin);
    }

    ButtonEvent sentinel { .pin = GPIO_NUM_NC, .pressed = false };
    buttonQueue.put(&sentinel, portMAX_DELAY);

    driverThread->join();
    driverThread = nullptr;
}

bool ButtonControl::startLvgl(lv_display_t* display) {
    if (deviceHandle != nullptr) {
        return false;
    }

    if (!startThread()) {
        return false;
    }

    deviceHandle = lv_indev_create();
    lv_indev_set_type(deviceHandle, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_driver_data(deviceHandle, this);
    lv_indev_set_read_cb(deviceHandle, readCallback);

    return true;
}

bool ButtonControl::stopLvgl() {
    if (deviceHandle == nullptr) {
        return false;
    }

    lv_indev_delete(deviceHandle);
    deviceHandle = nullptr;

    stopThread();

    return true;
}
