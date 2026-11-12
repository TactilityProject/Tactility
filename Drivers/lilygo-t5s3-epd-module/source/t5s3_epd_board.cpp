// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Signalling of the ED047TC1 panel on the LILYGO T5 4.7 Inch E-Paper S3.
// The sequences and timings follow the epdiy project (https://github.com/vroland/epdiy).
//
// The panel is controlled by a 74HCT4094 shift register (latch, output enable, mode, start of frame and the
// panel supplies), the CKV gate clock (RMT peripheral) and an 8 bit bus (LCD peripheral in i80 mode) that
// carries the 2 bit actions of 4 pixels per byte. Rows are sent one at a time. The data of a row is applied by the
// CKV pulse after the one that was running during its transfer.

#include "t5s3_epd_board.h"

#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_lcd_io_i80.h>
#include <esp_lcd_panel_io.h>
#include <esp_rom_sys.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

#include <cstring>

static constexpr gpio_num_t PIN_REGISTER_DATA = GPIO_NUM_13;
static constexpr gpio_num_t PIN_REGISTER_CLOCK = GPIO_NUM_12;
static constexpr gpio_num_t PIN_REGISTER_STROBE = GPIO_NUM_0;
static constexpr gpio_num_t PIN_CKV = GPIO_NUM_38;
// Start pulse of a row, driven as the data/command line of the bus
static constexpr gpio_num_t PIN_STH = GPIO_NUM_40;
// Pixel clock, driven as the write line of the bus
static constexpr gpio_num_t PIN_CKH = GPIO_NUM_41;
static constexpr gpio_num_t BUS_PINS[8] = {GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_8, GPIO_NUM_1};

// A row has headroom after the pixel data for the timing of the source driver
static constexpr int BUS_TRANSFER_BYTES = ((T5S3_EPD_WIDTH + 32) / 4 + 3) & ~3;

// Time base of the CKV pulses, 10 MHz
static constexpr uint32_t PULSE_RESOLUTION_HZ = 10000000;
// Low time after the pulse of a row in ticks
static constexpr uint32_t ROW_LOW_TICKS = 50;

// Limit for waiting on a flag that an interrupt handler sets, so that a lost interrupt fails the frame instead of spinning forever
static constexpr uint32_t WAIT_SPIN_LIMIT = 20000000;

struct Board {
    uint8_t* row_buffer[2];
    int current;
    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_panel_io_handle_t io;
    rmt_channel_handle_t pulse_channel;
    rmt_encoder_handle_t pulse_encoder;
    volatile bool transfer_done;
    volatile bool pulse_done;
    bool failed;
    // Shift register
    bool latch;
    bool output_enable;
    bool mode;
    bool start_of_frame;
    bool power_disable;
    bool positive_power;
    bool negative_power;
    // Rows written since the last row that carried data
    int rows_skipped;
    rmt_symbol_word_t pulse_symbol;
};

// Interrupt handlers use it, so it stays in internal RAM
static Board board;

static inline void IRAM_ATTR pin_set(gpio_num_t pin, uint32_t level) {
    gpio_ll_set_level(GPIO_LL_GET_HW(GPIO_PORT_0), pin, level);
}

static bool IRAM_ATTR on_pulse_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void*) {
    board.pulse_done = true;
    return false;
}

static bool IRAM_ATTR on_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    board.transfer_done = true;
    return false;
}

// After a failure nothing is waited for anymore, so the rest of the frame ends quickly
static void IRAM_ATTR wait_for(volatile bool& flag) {
    uint32_t spins = 0;
    while (!flag) {
        if (board.failed || ++spins > WAIT_SPIN_LIMIT) {
            board.failed = true;
            flag = true;
        }
    }
}

// region Shift register

static void IRAM_ATTR push_register_bit(bool bit) {
    pin_set(PIN_REGISTER_CLOCK, 0);
    pin_set(PIN_REGISTER_DATA, bit ? 1 : 0);
    pin_set(PIN_REGISTER_CLOCK, 1);
}

static void IRAM_ATTR push_register() {
    pin_set(PIN_REGISTER_STROBE, 0);
    push_register_bit(board.output_enable);
    push_register_bit(board.mode);
    // Always set
    push_register_bit(true);
    push_register_bit(board.start_of_frame);
    push_register_bit(board.negative_power);
    push_register_bit(board.positive_power);
    push_register_bit(board.power_disable);
    push_register_bit(board.latch);
    pin_set(PIN_REGISTER_STROBE, 1);
}

// endregion

// region CKV pulses and row transfer

// Pulses the gate clock for high_ticks and keeps it low for low_ticks, repeated for the given number of pulses.
// Without a high time the line is high for low_ticks.
static void IRAM_ATTR pulse_ckv(uint32_t high_ticks, uint32_t low_ticks, bool wait, int count = 1) {
    wait_for(board.pulse_done);
    rmt_symbol_word_t& symbol = board.pulse_symbol;
    if (high_ticks > 0) {
        symbol.duration0 = high_ticks;
        symbol.level0 = 1;
        symbol.duration1 = low_ticks;
        symbol.level1 = 0;
    } else {
        symbol.duration0 = low_ticks;
        symbol.level0 = 1;
        symbol.duration1 = 0;
        symbol.level1 = 0;
    }
    board.pulse_done = false;
    rmt_transmit_config_t config = {};
    config.loop_count = count;
    if (rmt_transmit(board.pulse_channel, board.pulse_encoder, &symbol, sizeof(symbol), &config) != ESP_OK) {
        board.pulse_done = true;
        board.failed = true;
        return;
    }
    if (wait) {
        wait_for(board.pulse_done);
    }
}

static void IRAM_ATTR pulse_ckv_us(uint32_t high_us, uint32_t low_us, bool wait) {
    pulse_ckv(high_us * 10, low_us * 10, wait);
}

static void IRAM_ATTR start_transfer() {
    board.transfer_done = false;
    if (esp_lcd_panel_io_tx_color(board.io, 0, board.row_buffer[board.current], BUS_TRANSFER_BYTES) != ESP_OK) {
        board.transfer_done = true;
        board.failed = true;
    }
}

// Applies the data that was sent last and starts the transfer of the current row buffer while the CKV pulse runs
static void IRAM_ATTR output_row(uint32_t hold_ticks) {
    wait_for(board.transfer_done);
    wait_for(board.pulse_done);
    board.latch = true;
    push_register();
    board.latch = false;
    push_register();
    pulse_ckv(hold_ticks, ROW_LOW_TICKS, false);
    start_transfer();
    board.current ^= 1;
}

static void IRAM_ATTR write_row(uint32_t hold_ticks) {
    output_row(hold_ticks);
    board.rows_skipped = 0;
}

// Skips a row after rows with data. The first two skipped rows push blank data through the pipeline.
static void IRAM_ATTR skip_row(uint32_t hold_ticks) {
    memset(board.row_buffer[board.current], 0, T5S3_EPD_BUS_ROW_BYTES);
    output_row(hold_ticks);
    board.rows_skipped++;
}

// Clocks the gate driver through rows that carry no data with one train of pulses
static void IRAM_ATTR skip_rows(int count) {
    pulse_ckv(45, 5, false, count);
    board.rows_skipped += count;
}

static void IRAM_ATTR start_frame() {
    wait_for(board.transfer_done);
    wait_for(board.pulse_done);
    board.rows_skipped = 0;

    board.mode = true;
    push_register();
    pulse_ckv_us(1, 1, true);

    // The start of frame line must go low and high again while the long pulse runs
    board.start_of_frame = false;
    push_register();
    pulse_ckv_us(1000, 100, false);
    board.start_of_frame = true;
    push_register();
    for (int i = 0; i < 4; i++) {
        pulse_ckv_us(1, 1, true);
    }

    board.output_enable = true;
    push_register();
}

static void IRAM_ATTR end_frame() {
    board.start_of_frame = false;
    push_register();
    for (int i = 0; i < 5; i++) {
        pulse_ckv_us(1, 1, true);
    }
    board.mode = false;
    push_register();
    pulse_ckv_us(0, 10, true);
    board.output_enable = false;
    push_register();
    for (int i = 0; i < 3; i++) {
        pulse_ckv_us(1, 1, true);
    }
}

// endregion

// region Frames

void t5s3_epd_board_fill_rows(uint8_t value) {
    wait_for(board.transfer_done);
    memset(board.row_buffer[0], value, T5S3_EPD_BUS_ROW_BYTES);
    memset(board.row_buffer[1], value, T5S3_EPD_BUS_ROW_BYTES);
}

static inline bool IRAM_ATTR has_data(int y, int y0, int y1, const bool* rows) {
    return y >= y0 && y < y1 && (rows == nullptr || rows[y]);
}

bool IRAM_ATTR t5s3_epd_board_frame(int y0, int y1, const bool* rows, uint32_t hold_ticks, T5s3EpdRowFn fill, void* context) {
    board.failed = false;
    start_frame();
    int y = 0;
    bool ends_with_train = false;
    while (y < T5S3_EPD_HEIGHT && !board.failed) {
        ends_with_train = false;
        if (has_data(y, y0, y1, rows)) {
            // The other buffer may still be in flight while this one is filled
            if (fill != nullptr) {
                fill(y, board.row_buffer[board.current], context);
            }
            write_row(hold_ticks);
            y++;
        } else if (board.rows_skipped < 2) {
            skip_row(hold_ticks);
            y++;
        } else {
            int run = 1;
            while (y + run < T5S3_EPD_HEIGHT && !has_data(y + run, y0, y1, rows)) {
                run++;
            }
            skip_rows(run);
            ends_with_train = true;
            y += run;
        }
    }
    // The last row is applied by one more pulse
    if (board.rows_skipped == 0 && !board.failed) {
        write_row(hold_ticks);
    } else if (ends_with_train) {
        // The end of the frame must not overlap the train of pulses of the skipped rows
        wait_for(board.pulse_done);
    }
    end_frame();
    return !board.failed;
}

// endregion

// region Setup

static bool setup_register_pins() {
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << PIN_REGISTER_DATA) | (1ULL << PIN_REGISTER_CLOCK) | (1ULL << PIN_REGISTER_STROBE);
    config.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&config) != ESP_OK) {
        return false;
    }
    pin_set(PIN_REGISTER_STROBE, 0);
    return true;
}

static bool setup_bus(uint32_t pixel_clock_hz) {
    esp_lcd_i80_bus_config_t bus_config = {};
    bus_config.dc_gpio_num = PIN_STH;
    bus_config.wr_gpio_num = PIN_CKH;
    bus_config.clk_src = LCD_CLK_SRC_DEFAULT;
    for (int i = 0; i < 8; i++) {
        bus_config.data_gpio_nums[i] = BUS_PINS[i];
    }
    bus_config.bus_width = 8;
    bus_config.max_transfer_bytes = BUS_TRANSFER_BYTES;
    if (esp_lcd_new_i80_bus(&bus_config, &board.bus) != ESP_OK) {
        return false;
    }

    // The data/command line is low when idle, high during a dummy command phase and low during the data of a row
    esp_lcd_panel_io_i80_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_NC;
    io_config.pclk_hz = pixel_clock_hz;
    io_config.trans_queue_depth = 4;
    io_config.on_color_trans_done = on_transfer_done;
    io_config.lcd_cmd_bits = 10;
    io_config.lcd_param_bits = 0;
    io_config.dc_levels.dc_idle_level = 0;
    io_config.dc_levels.dc_cmd_level = 1;
    io_config.dc_levels.dc_dummy_level = 0;
    io_config.dc_levels.dc_data_level = 0;
    return esp_lcd_new_panel_io_i80(board.bus, &io_config, &board.io) == ESP_OK;
}

static bool setup_pulse() {
    rmt_tx_channel_config_t channel_config = {};
    channel_config.gpio_num = PIN_CKV;
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = PULSE_RESOLUTION_HZ;
    channel_config.mem_block_symbols = 48;
    channel_config.trans_queue_depth = 1;
    if (rmt_new_tx_channel(&channel_config, &board.pulse_channel) != ESP_OK) {
        return false;
    }
    rmt_copy_encoder_config_t encoder_config = {};
    if (rmt_new_copy_encoder(&encoder_config, &board.pulse_encoder) != ESP_OK) {
        return false;
    }
    rmt_tx_event_callbacks_t callbacks = {};
    callbacks.on_trans_done = on_pulse_done;
    if (rmt_tx_register_event_callbacks(board.pulse_channel, &callbacks, nullptr) != ESP_OK) {
        return false;
    }
    return rmt_enable(board.pulse_channel) == ESP_OK;
}

bool t5s3_epd_board_init(uint32_t pixel_clock_hz) {
    for (auto& buffer: board.row_buffer) {
        buffer = static_cast<uint8_t*>(heap_caps_aligned_calloc(16, 1, BUS_TRANSFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (board.row_buffer[0] == nullptr || board.row_buffer[1] == nullptr || !setup_register_pins() || !setup_bus(pixel_clock_hz) || !setup_pulse()) {
        t5s3_epd_board_deinit();
        return false;
    }

    board.latch = false;
    board.output_enable = false;
    board.mode = false;
    board.start_of_frame = true;
    board.power_disable = true;
    board.positive_power = false;
    board.negative_power = false;
    board.current = 0;
    board.transfer_done = true;
    board.pulse_done = true;
    push_register();
    return true;
}

void t5s3_epd_board_deinit() {
    if (board.pulse_channel != nullptr) {
        // Both calls fail harmlessly when the channel was not enabled
        rmt_disable(board.pulse_channel);
        rmt_del_channel(board.pulse_channel);
        board.pulse_channel = nullptr;
    }
    if (board.pulse_encoder != nullptr) {
        rmt_del_encoder(board.pulse_encoder);
        board.pulse_encoder = nullptr;
    }
    if (board.io != nullptr) {
        esp_lcd_panel_io_del(board.io);
        board.io = nullptr;
    }
    if (board.bus != nullptr) {
        esp_lcd_del_i80_bus(board.bus);
        board.bus = nullptr;
    }
    heap_caps_free(board.row_buffer[0]);
    heap_caps_free(board.row_buffer[1]);
    board.row_buffer[0] = nullptr;
    board.row_buffer[1] = nullptr;
}

// endregion

// region Power

void t5s3_epd_board_power_on() {
    board.power_disable = false;
    push_register();
    esp_rom_delay_us(100);
    board.negative_power = true;
    push_register();
    esp_rom_delay_us(500);
    board.positive_power = true;
    push_register();
    esp_rom_delay_us(100);
    board.start_of_frame = true;
    push_register();
}

void t5s3_epd_board_power_off() {
    board.positive_power = false;
    push_register();
    esp_rom_delay_us(10);
    board.negative_power = false;
    push_register();
    esp_rom_delay_us(100);
    board.start_of_frame = false;
    board.output_enable = false;
    board.mode = false;
    board.power_disable = true;
    push_register();
}

// endregion
