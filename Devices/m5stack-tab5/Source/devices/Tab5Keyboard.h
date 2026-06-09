#pragma once
#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/Timer.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/queue.h>

class Tab5Keyboard final : public tt::hal::keyboard::KeyboardDevice {
    static constexpr uint8_t    I2C_ADDRESS = 0x6D;
    static constexpr gpio_num_t INT_PIN     = GPIO_NUM_50;

    // Software key-repeat timing
    static constexpr uint32_t REPEAT_INITIAL_MS = 400;
    static constexpr uint32_t REPEAT_RATE_MS    = 80;

    i2c_master_bus_handle_t i2cBus = nullptr;
    i2c_master_dev_handle_t i2cDev = nullptr;

    lv_indev_t* kbHandle = nullptr;
    QueueHandle_t queue = nullptr;
    std::unique_ptr<tt::Timer> inputTimer;

    bool symActive  = false;  // held while Sym is physically down
    bool aaSticky   = false;  // latched on single Aa tap, cleared after next non-modifier key
    bool aaHeld     = false;  // true while Aa is physically held
    bool ctrlHeld   = false;  // true while Ctrl is physically held

    // IRQ-driven event gating
    volatile bool irqPending = false;
    bool irqConfigured = false;

    // Software key-repeat state (tracked by position to survive modifier changes)
    uint32_t repeatKey      = 0;
    uint8_t  repeatRow      = 0xFF;
    uint8_t  repeatCol      = 0xFF;
    uint32_t repeatStartMs  = 0;
    uint32_t repeatLastMs   = 0;

    bool readReg(uint8_t reg, uint8_t& value) const;
    bool writeReg(uint8_t reg, uint8_t value) const;
    void updateLeds();

    bool configureIrqPin();
    void removeIrqPin();
    static void IRAM_ATTR irqHandler(void* arg);

    void drainEvents();
    void processKeyboard();
    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:
    Tab5Keyboard() {
        queue = xQueueCreate(20, sizeof(uint32_t));
    }
    ~Tab5Keyboard() override;  // defined in .cpp: calls removeIrqPin() + vQueueDelete(queue)

    std::string getName() const override { return "Tab5Keyboard"; }
    std::string getDescription() const override { return "M5Stack Tab5 Keyboard addon"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;
    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return kbHandle; }
};
