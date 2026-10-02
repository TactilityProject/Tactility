// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Minimal driver for the ED047TC1 panel of the LILYGO T5 4.7 Inch E-Paper S3.
// The sequences and timings follow the epdiy project (https://github.com/vroland/epdiy).
//
// The panel is controlled by a 74HCT4094 shift register (latch, output enable, mode, start of frame and the
// panel supplies), the CKV gate clock (RMT peripheral) and an 8 bit bus (LCD peripheral in i80 mode) that
// carries the 2 bit actions of 4 pixels per byte. Rows are sent one at a time. The data of a row is applied by the
// CKV pulse after the one that was running during its transfer.

#include "t5s3_epd.h"

#include <tactility/log.h>

#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <esp_heap_caps.h>
#include <esp_lcd_io_i80.h>
#include <esp_lcd_panel_io.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

#include <cstring>

#define TAG "T5s3Epd"

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
// More phases drive the pixels further into black or white, which leaves less ghosting but makes fast updates slower
static constexpr uint32_t FAST_PHASES = 10;
// Hold time of a phase of the fast update in ticks
static constexpr uint32_t FAST_HOLD_TICKS = 1000;
// Hold time of the frames that flash the panel to white
static constexpr uint32_t CLEAR_HOLD_TICKS = 120;
static constexpr int CLEAR_CYCLES = 3;
static constexpr int CLEAR_DARK_FRAMES = 10;
static constexpr int CLEAR_LIGHT_FRAMES = 10;
static constexpr int CLEAR_REST_FRAMES = 2;
static constexpr uint8_t ALL_DARK = 0x55;
static constexpr uint8_t ALL_LIGHT = 0xAA;

// Limit for waiting on a flag that an interrupt handler sets, so that a lost interrupt fails the update instead of spinning forever
static constexpr uint32_t WAIT_SPIN_LIMIT = 20000000;

struct Engine {
    uint8_t* front = nullptr;
    uint8_t* back = nullptr;
    uint8_t* row_buffer[2] = {nullptr, nullptr};
    int current = 0;
    esp_lcd_i80_bus_handle_t bus = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    rmt_channel_handle_t pulse_channel = nullptr;
    rmt_encoder_handle_t pulse_encoder = nullptr;
    volatile bool transfer_done = true;
    volatile bool pulse_done = true;
    bool failed = false;
    // Shift register
    bool latch = false;
    bool output_enable = false;
    bool mode = false;
    bool start_of_frame = true;
    bool power_disable = true;
    bool positive_power = false;
    bool negative_power = false;
    // Rows written since the last row that carried data
    int rows_skipped = 0;
    bool row_dirty[T5S3_EPD_HEIGHT];
    alignas(4) uint8_t changes[T5S3_EPD_FB_ROW_BYTES];
    alignas(4) uint8_t line_mask[T5S3_EPD_BUS_ROW_BYTES];
};

static Engine engine;
static rmt_symbol_word_t pulse_symbol;

static inline void IRAM_ATTR pin_set(gpio_num_t pin, uint32_t level) {
    gpio_ll_set_level(GPIO_LL_GET_HW(GPIO_PORT_0), pin, level);
}

static bool IRAM_ATTR on_pulse_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void*) {
    engine.pulse_done = true;
    return false;
}

static bool IRAM_ATTR on_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    engine.transfer_done = true;
    return false;
}

// After a failure nothing is waited for anymore, so the rest of the update ends quickly
static void IRAM_ATTR wait_for(volatile bool& flag) {
    uint32_t spins = 0;
    while (!flag) {
        if (engine.failed || ++spins > WAIT_SPIN_LIMIT) {
            engine.failed = true;
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
    push_register_bit(engine.output_enable);
    push_register_bit(engine.mode);
    // Always set
    push_register_bit(true);
    push_register_bit(engine.start_of_frame);
    push_register_bit(engine.negative_power);
    push_register_bit(engine.positive_power);
    push_register_bit(engine.power_disable);
    push_register_bit(engine.latch);
    pin_set(PIN_REGISTER_STROBE, 1);
}

// endregion

// region CKV pulses and row transfer

// Pulses the gate clock for high_ticks and keeps it low for low_ticks, repeated for the given number of pulses.
// Without a high time the line is high for low_ticks.
static void IRAM_ATTR pulse_ckv(uint32_t high_ticks, uint32_t low_ticks, bool wait, int count = 1) {
    wait_for(engine.pulse_done);
    if (high_ticks > 0) {
        pulse_symbol.duration0 = high_ticks;
        pulse_symbol.level0 = 1;
        pulse_symbol.duration1 = low_ticks;
        pulse_symbol.level1 = 0;
    } else {
        pulse_symbol.duration0 = low_ticks;
        pulse_symbol.level0 = 1;
        pulse_symbol.duration1 = 0;
        pulse_symbol.level1 = 0;
    }
    engine.pulse_done = false;
    rmt_transmit_config_t config = {};
    config.loop_count = count;
    if (rmt_transmit(engine.pulse_channel, engine.pulse_encoder, &pulse_symbol, sizeof(pulse_symbol), &config) != ESP_OK) {
        engine.pulse_done = true;
        engine.failed = true;
        return;
    }
    if (wait) {
        wait_for(engine.pulse_done);
    }
}

static void IRAM_ATTR pulse_ckv_us(uint32_t high_us, uint32_t low_us, bool wait) {
    pulse_ckv(high_us * 10, low_us * 10, wait);
}

static void IRAM_ATTR start_transfer() {
    engine.transfer_done = false;
    if (esp_lcd_panel_io_tx_color(engine.io, 0, engine.row_buffer[engine.current], BUS_TRANSFER_BYTES) != ESP_OK) {
        engine.transfer_done = true;
        engine.failed = true;
    }
}

// Applies the data that was sent last and starts the transfer of the current row buffer while the CKV pulse runs
static void IRAM_ATTR output_row(uint32_t hold_ticks) {
    wait_for(engine.transfer_done);
    wait_for(engine.pulse_done);
    engine.latch = true;
    push_register();
    engine.latch = false;
    push_register();
    pulse_ckv(hold_ticks, ROW_LOW_TICKS, false);
    start_transfer();
    engine.current ^= 1;
}

static void IRAM_ATTR write_row(uint32_t hold_ticks) {
    output_row(hold_ticks);
    engine.rows_skipped = 0;
}

// Skips a row after rows with data. The first two skipped rows push blank data through the pipeline.
static void IRAM_ATTR skip_row(uint32_t hold_ticks) {
    memset(engine.row_buffer[engine.current], 0, T5S3_EPD_BUS_ROW_BYTES);
    output_row(hold_ticks);
    engine.rows_skipped++;
}

// Clocks the gate driver through rows that carry no data with one train of pulses
static void IRAM_ATTR skip_rows(int count) {
    pulse_ckv(45, 5, false, count);
    engine.rows_skipped += count;
}

static void IRAM_ATTR start_frame() {
    wait_for(engine.transfer_done);
    wait_for(engine.pulse_done);
    engine.rows_skipped = 0;

    engine.mode = true;
    push_register();
    pulse_ckv_us(1, 1, true);

    // The start of frame line must go low and high again while the long pulse runs
    engine.start_of_frame = false;
    push_register();
    pulse_ckv_us(1000, 100, false);
    engine.start_of_frame = true;
    push_register();
    for (int i = 0; i < 4; i++) {
        pulse_ckv_us(1, 1, true);
    }

    engine.output_enable = true;
    push_register();
}

static void IRAM_ATTR end_frame() {
    engine.start_of_frame = false;
    push_register();
    for (int i = 0; i < 5; i++) {
        pulse_ckv_us(1, 1, true);
    }
    engine.mode = false;
    push_register();
    pulse_ckv_us(0, 10, true);
    engine.output_enable = false;
    push_register();
    for (int i = 0; i < 3; i++) {
        pulse_ckv_us(1, 1, true);
    }
}

// endregion

// region Frames

static void IRAM_ATTR draw_frame(const uint8_t* table, uint32_t hold_ticks) {
    start_frame();
    int y = 0;
    bool ends_with_train = false;
    while (y < T5S3_EPD_HEIGHT && !engine.failed) {
        ends_with_train = false;
        if (engine.row_dirty[y]) {
            t5s3_epd_prepare_row(
                engine.row_buffer[engine.current],
                engine.front + y * T5S3_EPD_FB_ROW_BYTES,
                engine.back + y * T5S3_EPD_FB_ROW_BYTES,
                table,
                engine.line_mask
            );
            write_row(hold_ticks);
            y++;
        } else if (engine.rows_skipped < 2) {
            skip_row(hold_ticks);
            y++;
        } else {
            int run = 1;
            while (y + run < T5S3_EPD_HEIGHT && !engine.row_dirty[y + run]) {
                run++;
            }
            skip_rows(run);
            ends_with_train = true;
            y += run;
        }
    }
    // The last row is applied by one more pulse
    if (engine.rows_skipped == 0 && !engine.failed) {
        write_row(hold_ticks);
    } else if (ends_with_train) {
        // The end of the frame must not overlap the train of pulses of the skipped rows
        wait_for(engine.pulse_done);
    }
    end_frame();
}

static void IRAM_ATTR draw_uniform_frame(uint8_t pattern, uint32_t hold_ticks) {
    memset(engine.row_buffer[0], pattern, T5S3_EPD_BUS_ROW_BYTES);
    memset(engine.row_buffer[1], pattern, T5S3_EPD_BUS_ROW_BYTES);
    start_frame();
    for (int y = 0; y < T5S3_EPD_HEIGHT && !engine.failed; y++) {
        write_row(hold_ticks);
    }
    if (!engine.failed) {
        write_row(hold_ticks);
    }
    end_frame();
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
    if (esp_lcd_new_i80_bus(&bus_config, &engine.bus) != ESP_OK) {
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
    return esp_lcd_new_panel_io_i80(engine.bus, &io_config, &engine.io) == ESP_OK;
}

static bool setup_pulse() {
    rmt_tx_channel_config_t channel_config = {};
    channel_config.gpio_num = PIN_CKV;
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = PULSE_RESOLUTION_HZ;
    channel_config.mem_block_symbols = 48;
    channel_config.trans_queue_depth = 1;
    if (rmt_new_tx_channel(&channel_config, &engine.pulse_channel) != ESP_OK) {
        return false;
    }
    rmt_copy_encoder_config_t encoder_config = {};
    if (rmt_new_copy_encoder(&encoder_config, &engine.pulse_encoder) != ESP_OK) {
        return false;
    }
    rmt_tx_event_callbacks_t callbacks = {};
    callbacks.on_trans_done = on_pulse_done;
    if (rmt_tx_register_event_callbacks(engine.pulse_channel, &callbacks, nullptr) != ESP_OK) {
        return false;
    }
    return rmt_enable(engine.pulse_channel) == ESP_OK;
}

bool t5s3_epd_init(uint32_t pixel_clock_hz) {
    if (engine.front != nullptr) {
        return true;
    }

    engine.front = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, T5S3_EPD_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    engine.back = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, T5S3_EPD_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    for (auto& buffer: engine.row_buffer) {
        buffer = static_cast<uint8_t*>(heap_caps_aligned_calloc(16, 1, BUS_TRANSFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (engine.front == nullptr || engine.back == nullptr || engine.row_buffer[0] == nullptr || engine.row_buffer[1] == nullptr) {
        LOG_E(TAG, "Out of memory");
        t5s3_epd_deinit();
        return false;
    }
    memset(engine.front, 0xFF, T5S3_EPD_FB_BYTES);
    memset(engine.back, 0xFF, T5S3_EPD_FB_BYTES);

    if (!setup_register_pins() || !setup_bus(pixel_clock_hz) || !setup_pulse()) {
        LOG_E(TAG, "Failed to set up the pins and peripherals");
        t5s3_epd_deinit();
        return false;
    }

    engine.latch = false;
    engine.output_enable = false;
    engine.mode = false;
    engine.start_of_frame = true;
    engine.power_disable = true;
    engine.positive_power = false;
    engine.negative_power = false;
    engine.current = 0;
    engine.transfer_done = true;
    engine.pulse_done = true;
    push_register();
    return true;
}

void t5s3_epd_deinit() {
    if (engine.pulse_channel != nullptr) {
        // Both calls fail harmlessly when the channel was not enabled
        rmt_disable(engine.pulse_channel);
        rmt_del_channel(engine.pulse_channel);
        engine.pulse_channel = nullptr;
    }
    if (engine.pulse_encoder != nullptr) {
        rmt_del_encoder(engine.pulse_encoder);
        engine.pulse_encoder = nullptr;
    }
    if (engine.io != nullptr) {
        esp_lcd_panel_io_del(engine.io);
        engine.io = nullptr;
    }
    if (engine.bus != nullptr) {
        esp_lcd_del_i80_bus(engine.bus);
        engine.bus = nullptr;
    }
    heap_caps_free(engine.front);
    heap_caps_free(engine.back);
    heap_caps_free(engine.row_buffer[0]);
    heap_caps_free(engine.row_buffer[1]);
    engine.front = nullptr;
    engine.back = nullptr;
    engine.row_buffer[0] = nullptr;
    engine.row_buffer[1] = nullptr;
}

// endregion

// region Power

void t5s3_epd_power_on() {
    engine.power_disable = false;
    push_register();
    esp_rom_delay_us(100);
    engine.negative_power = true;
    push_register();
    esp_rom_delay_us(500);
    engine.positive_power = true;
    push_register();
    esp_rom_delay_us(100);
    engine.start_of_frame = true;
    push_register();
}

void t5s3_epd_power_off() {
    engine.positive_power = false;
    push_register();
    esp_rom_delay_us(10);
    engine.negative_power = false;
    push_register();
    esp_rom_delay_us(100);
    engine.start_of_frame = false;
    engine.output_enable = false;
    engine.mode = false;
    engine.power_disable = true;
    push_register();
}

// endregion

// region Updates

uint8_t* t5s3_epd_framebuffer() {
    return engine.front;
}

void t5s3_epd_fill_white() {
    memset(engine.front, 0xFF, T5S3_EPD_FB_BYTES);
}

void t5s3_epd_invalidate() {
    auto* words = reinterpret_cast<uint32_t*>(engine.back);
    for (int i = 0; i < T5S3_EPD_FB_BYTES / 4; i++) {
        words[i] = ~words[i];
    }
}

bool t5s3_epd_update(T5s3EpdMode mode, int32_t y_start, int32_t y_end) {
    if (y_start < 0) {
        y_start = 0;
    }
    if (y_end > T5S3_EPD_HEIGHT) {
        y_end = T5S3_EPD_HEIGHT;
    }
    if (y_start >= y_end) {
        return true;
    }
    const int64_t started = esp_timer_get_time();

    // Find the rows and columns that changed
    memset(engine.row_dirty, 0, sizeof(engine.row_dirty));
    memset(engine.changes, 0, sizeof(engine.changes));
    auto* changes = reinterpret_cast<uint32_t*>(engine.changes);
    bool any_changes = false;
    for (int y = y_start; y < y_end; y++) {
        const auto* to_row = reinterpret_cast<const uint32_t*>(engine.front + y * T5S3_EPD_FB_ROW_BYTES);
        const auto* from_row = reinterpret_cast<const uint32_t*>(engine.back + y * T5S3_EPD_FB_ROW_BYTES);
        uint32_t row_changes = 0;
        for (int i = 0; i < T5S3_EPD_FB_ROW_BYTES / 4; i++) {
            const uint32_t difference = to_row[i] ^ from_row[i];
            changes[i] |= difference;
            row_changes |= difference;
        }
        engine.row_dirty[y] = row_changes != 0;
        any_changes = any_changes || row_changes != 0;
    }
    if (!any_changes) {
        return true;
    }
    t5s3_epd_build_line_mask(engine.line_mask, engine.changes);

    engine.failed = false;
    uint8_t table[256];
    if (mode == T5s3EpdMode::Fast) {
        t5s3_epd_build_fast_table(table);
        for (uint32_t phase = 0; phase < FAST_PHASES && !engine.failed; phase++) {
            draw_frame(table, FAST_HOLD_TICKS);
            taskYIELD();
        }
    } else {
        // Long updates sleep between frames so that the idle task can feed the watchdog
        for (int phase = 0; phase < T5S3_EPD_QUALITY_PHASES && !engine.failed; phase++) {
            t5s3_epd_build_quality_table(table, phase, mode == T5s3EpdMode::Full);
            draw_frame(table, T5S3_EPD_QUALITY_HOLD[phase]);
            vTaskDelay(1);
        }
    }

    // A failed update leaves the back buffer alone, so the next update draws the same rows again
    if (engine.failed) {
        LOG_E(TAG, "Update failed, a peripheral did not respond");
        return false;
    }

    for (int y = y_start; y < y_end; y++) {
        if (engine.row_dirty[y]) {
            memcpy(engine.back + y * T5S3_EPD_FB_ROW_BYTES, engine.front + y * T5S3_EPD_FB_ROW_BYTES, T5S3_EPD_FB_ROW_BYTES);
        }
    }

    const int elapsed_ms = static_cast<int>((esp_timer_get_time() - started) / 1000);
    // Quality and slow updates are logged at info level, frequent fast updates would flood the log
    if (mode != T5s3EpdMode::Fast || elapsed_ms > 150) {
        LOG_I(TAG, "Updated rows %d to %d in %d ms", static_cast<int>(y_start), static_cast<int>(y_end), elapsed_ms);
    } else {
        LOG_D(TAG, "Updated rows %d to %d in %d ms", static_cast<int>(y_start), static_cast<int>(y_end), elapsed_ms);
    }
    return true;
}

static void draw_clear_frames(uint8_t pattern, int count) {
    for (int i = 0; i < count && !engine.failed; i++) {
        draw_uniform_frame(pattern, CLEAR_HOLD_TICKS);
        vTaskDelay(1);
    }
}

bool t5s3_epd_clear() {
    const int64_t started = esp_timer_get_time();
    t5s3_epd_fill_white();
    if (!t5s3_epd_update(T5s3EpdMode::Full, 0, T5S3_EPD_HEIGHT)) {
        return false;
    }
    engine.failed = false;
    for (int cycle = 0; cycle < CLEAR_CYCLES; cycle++) {
        draw_clear_frames(ALL_DARK, CLEAR_DARK_FRAMES);
        draw_clear_frames(ALL_LIGHT, CLEAR_LIGHT_FRAMES);
        draw_clear_frames(0, CLEAR_REST_FRAMES);
    }
    if (engine.failed) {
        LOG_E(TAG, "Clear failed, a peripheral did not respond");
        return false;
    }
    LOG_I(TAG, "Cleared in %d ms", static_cast<int>((esp_timer_get_time() - started) / 1000));
    return true;
}

// endregion
