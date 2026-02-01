#pragma once

#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/Timer.h>

#include <Tca8418.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/queue.h>

class TpagerKeyboard final : public tt::hal::keyboard::KeyboardDevice {

    lv_indev_t* kbHandle = nullptr;
    gpio_num_t backlightPin = GPIO_NUM_NC;
    ledc_timer_t backlightTimer;
    ledc_channel_t backlightChannel;
    bool backlightOkay = false;
    int backlightImpulseDuty = 0;
    QueueHandle_t queue = nullptr;

    std::shared_ptr<Tca8418> keypad;
    std::unique_ptr<tt::Timer> inputTimer;
    std::unique_ptr<tt::Timer> backlightImpulseTimer;

    bool initBacklight(gpio_num_t pin, uint32_t frequencyHz, ledc_timer_t timer, ledc_channel_t channel);
    void processKeyboard();
    void processBacklightImpulse();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    explicit TpagerKeyboard(const std::shared_ptr<Tca8418>& tca) : keypad(tca) {
        queue = xQueueCreate(20, sizeof(char));
    }

    ~TpagerKeyboard() override {
        vQueueDelete(queue);
    }

    std::string getName() const override { return "T-Lora Pager Keyboard"; }
    std::string getDescription() const override { return "T-Lora Pager I2C keyboard with encoder"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return kbHandle; }

    bool setBacklightDuty(uint8_t duty);
    void makeBacklightImpulse();
};
