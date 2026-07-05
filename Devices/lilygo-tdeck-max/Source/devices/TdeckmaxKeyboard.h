#pragma once

#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/Timer.h>

#include <Tca8418.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/queue.h>

// QWERTY keyboard for the LilyGO T-Deck (Pro) Max.
//
// The keyboard is a TCA8418 4x10 matrix scanner on the shared I2C bus at 0x34.
// The keyboard reset line is wired through the XL9555 IO expander (P11), so it
// must be released before this device is started (see Configuration.cpp).
//
// Pins, address and keymap are taken from Xinyuan-LilyGO/T-Deck-MAX:
// examples/factory/peri_keypad.cpp and lib/TDeckMaxBoard/src/TDeckMaxBoard.h.
class TdeckmaxKeyboard final : public tt::hal::keyboard::KeyboardDevice {

    lv_indev_t* kbHandle = nullptr;
    gpio_num_t backlightPin = GPIO_NUM_NC;
    ledc_timer_t backlightTimer;
    ledc_channel_t backlightChannel;
    bool backlightOkay = false;
    int backlightImpulseDuty = 0;
    QueueHandle_t queue = nullptr;
    // LVGL only registers a key on a RELEASED->PRESSED edge, so readCallback
    // alternates press/release per queued key; this tracks the pending release.
    uint32_t lastKey = 0;
    bool lastKeyNeedsRelease = false;

    std::shared_ptr<Tca8418> keypad;
    std::unique_ptr<tt::Timer> inputTimer;
    std::unique_ptr<tt::Timer> backlightImpulseTimer;

    bool initBacklight(gpio_num_t pin, uint32_t frequencyHz, ledc_timer_t timer, ledc_channel_t channel);
    void processKeyboard();
    void processBacklightImpulse();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    explicit TdeckmaxKeyboard(const std::shared_ptr<Tca8418>& tca) : keypad(tca) {
        queue = xQueueCreate(20, sizeof(char));
    }

    ~TdeckmaxKeyboard() override {
        vQueueDelete(queue);
    }

    std::string getName() const override { return "T-Deck Max Keyboard"; }
    std::string getDescription() const override { return "T-Deck Max TCA8418 I2C matrix keyboard"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return kbHandle; }

    bool setBacklightDuty(uint8_t duty);
    void makeBacklightImpulse();
};
