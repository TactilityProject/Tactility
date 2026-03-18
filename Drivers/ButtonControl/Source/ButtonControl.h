#pragma once

#include <Tactility/hal/encoder/EncoderDevice.h>
#include <Tactility/hal/gpio/Gpio.h>
#include <Tactility/MessageQueue.h>
#include <Tactility/TactilityCore.h>
#include <Tactility/Thread.h>

#include <driver/gpio.h>

class ButtonControl final : public tt::hal::encoder::EncoderDevice {

public:

    enum class Event {
        ShortPress,
        LongPress
    };

    enum class Action {
        UiSelectNext,
        UiSelectPrevious,
        UiPressSelected,
        AppClose,
    };

    struct PinConfiguration {
        tt::hal::gpio::Pin pin;
        Event event;
        Action action;
    };

private:

    struct PinState {
        long pressStartTime = 0;
        long lastChangeTime = 0;
        bool pressState = false;
        bool triggerShortPress = false;
        bool triggerLongPress = false;
    };

    /** Queued from ISR to worker thread. pin == GPIO_NUM_NC is a shutdown sentinel. */
    struct ButtonEvent {
        gpio_num_t pin;
        bool pressed;
    };

    /** One entry per unique physical pin; addresses must remain stable after construction. */
    struct IsrArg {
        ButtonControl* self;
        gpio_num_t pin;
    };

    lv_indev_t* deviceHandle = nullptr;
    std::shared_ptr<tt::Thread> driverThread;
    tt::Mutex mutex;
    tt::MessageQueue buttonQueue;
    std::vector<PinConfiguration> pinConfigurations;
    std::vector<PinState> pinStates;
    std::vector<IsrArg> isrArgs; // one entry per unique physical pin

    static void updatePin(std::vector<PinConfiguration>::const_reference config, std::vector<PinState>::reference state, bool pressed);

    void driverThreadMain();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

    static void IRAM_ATTR gpioIsrHandler(void* arg);

    bool startThread();
    void stopThread();

public:

    explicit ButtonControl(const std::vector<PinConfiguration>& pinConfigurations);

    ~ButtonControl() override;

    std::string getName() const override { return "ButtonControl"; }
    std::string getDescription() const override { return "ButtonControl input driver"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    /** Could return nullptr if not started */
    lv_indev_t* getLvglIndev() override { return deviceHandle; }

    static std::shared_ptr<ButtonControl> createOneButtonControl(tt::hal::gpio::Pin pin) {
        return std::make_shared<ButtonControl>(std::vector {
            PinConfiguration {
                .pin = pin,
                .event = Event::ShortPress,
                .action = Action::UiSelectNext
            },
            PinConfiguration {
                .pin = pin,
                .event = Event::LongPress,
                .action = Action::UiPressSelected
            }
        });
    }

    static std::shared_ptr<ButtonControl> createTwoButtonControl(tt::hal::gpio::Pin primaryPin, tt::hal::gpio::Pin secondaryPin) {
        return std::make_shared<ButtonControl>(std::vector {
            PinConfiguration {
                .pin = primaryPin,
                .event = Event::ShortPress,
                .action = Action::UiPressSelected
            },
            PinConfiguration {
                .pin = primaryPin,
                .event = Event::LongPress,
                .action = Action::AppClose
            },
            PinConfiguration {
                .pin = secondaryPin,
                .event = Event::ShortPress,
                .action = Action::UiSelectNext
            },
            PinConfiguration {
                .pin = secondaryPin,
                .event = Event::LongPress,
                .action = Action::UiSelectPrevious
            }
        });
    }
};
