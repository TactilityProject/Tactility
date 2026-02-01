#pragma once

#include <Tactility/hal/encoder/EncoderDevice.h>
#include <Tactility/hal/gpio/Gpio.h>
#include <Tactility/TactilityCore.h>
#include <Tactility/Thread.h>

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
        long pressReleaseTime = 0;
        bool pressState = false;
        bool triggerShortPress = false;
        bool triggerLongPress = false;
    };

    lv_indev_t* deviceHandle = nullptr;
    std::shared_ptr<tt::Thread> driverThread;
    bool interruptDriverThread = false;
    tt::Mutex mutex;
    std::vector<PinConfiguration> pinConfigurations;
    std::vector<PinState> pinStates;

    bool shouldInterruptDriverThread() const;

    static void updatePin(std::vector<PinConfiguration>::const_reference value, std::vector<PinState>::reference pin_state);

    void driverThreadMain();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

    void startThread();
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
