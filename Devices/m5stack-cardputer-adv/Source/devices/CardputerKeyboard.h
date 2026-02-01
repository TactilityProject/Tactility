#pragma once

#include <Tactility/hal/keyboard/KeyboardDevice.h>

#include <Tca8418.h>

#include <Tactility/Timer.h>
#include <freertos/queue.h>

class CardputerKeyboard final : public tt::hal::keyboard::KeyboardDevice {

    lv_indev_t* kbHandle = nullptr;
    QueueHandle_t queue = nullptr;

    std::shared_ptr<Tca8418> keypad;
    std::unique_ptr<tt::Timer> inputTimer;

    void processKeyboard();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

    /**
     * Remaps wiring coordinates to keyboard mapping coordinates.
     * Wiring is 7x8 (rows & colums), but our keyboard definition is 4x14)
     */
    static void remap(uint8_t& row, uint8_t& column);

public:

    explicit CardputerKeyboard(const std::shared_ptr<Tca8418>& tca) : keypad(tca) {
        queue = xQueueCreate(20, sizeof(char));
    }

    ~CardputerKeyboard() override {
        vQueueDelete(queue);
    }

    std::string getName() const override { return "TCA8418"; }
    std::string getDescription() const override { return "TCA8418 I2C keyboard"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return kbHandle; }
};
