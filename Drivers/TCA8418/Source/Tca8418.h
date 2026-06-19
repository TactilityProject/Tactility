#pragma once

#include <array>

#include <Tactility/hal/i2c/I2cDevice.h>

constexpr auto TCA8418_ADDRESS = 0x34U;
constexpr auto KEY_EVENT_LIST_SIZE = 10;

/**
 * See https://www.ti.com/lit/ds/symlink/tca8418.pdf
 */
class Tca8418 final : public tt::hal::i2c::I2cDevice {

    uint8_t tca8418_address;
    uint32_t last_update_micros;
    uint32_t this_update_micros;

    uint8_t new_pressed_keys_count;

    void clear_released_list();
    void clear_pressed_list();
    void add_pressed_key(uint8_t row, uint8_t col);
    void add_released_key(uint8_t row, uint8_t col);
    void remove_pressed_key(uint8_t row, uint8_t col);
    void write(uint8_t register_address, uint8_t data);
    bool read(uint8_t register_address, uint8_t* data);

    bool initMatrix(uint8_t rows, uint8_t columns);

public:

    struct PressedKey {
        uint8_t row;
        uint8_t col;
        uint32_t hold_time;
    };

    struct ReleasedKey {
        uint8_t row;
        uint8_t col;
    };

    std::string getName() const final { return "TCA8418"; }

    std::string getDescription() const final { return "I2C-controlled keyboard scan IC"; }

    explicit Tca8418(::Device* controller) : I2cDevice(controller, TCA8418_ADDRESS) {
        delta_micros = 0;
        last_update_micros = 0;
        this_update_micros = 0;
    }

    ~Tca8418() {}

    uint8_t num_rows;
    uint8_t num_cols;

    uint32_t delta_micros;

    std::array<PressedKey, KEY_EVENT_LIST_SIZE> pressed_list;
    std::array<ReleasedKey, KEY_EVENT_LIST_SIZE> released_list;
    uint8_t pressed_key_count;
    uint8_t released_key_count;

    void init(uint8_t numrows, uint8_t numcols);
    bool update();
    uint8_t get_key_event();
    bool button_pressed(uint8_t row, uint8_t button_bit_position);
    bool button_released(uint8_t row, uint8_t button_bit_position);
    bool button_held(uint8_t row, uint8_t button_bit_position);
};
