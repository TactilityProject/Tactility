// SPDX-License-Identifier: Apache-2.0
#include "gdep103tc2_bus.h"
#include "gdep103tc2_panel.h"

#include <tactility/log.h>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_lcd_panel_io.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>

#define TAG "Gdep103tc2Bus"

static constexpr int ROW_COMMAND_BITS = BUS_WIDTH;
static constexpr int ROW_COMMAND = 0;
static constexpr int TRANSACTION_QUEUE_DEPTH = 4;
static constexpr size_t DMA_BURST_BYTES = 32;

// A row takes about 10 us on the bus
static constexpr int64_t ROW_TIMEOUT_US = 5000;

static bool IRAM_ATTR on_row_sent(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    static_cast<Gdep103tc2Bus*>(user_ctx)->dma_done = true;
    return false;
}

static error_t configure_control_pins(const Gdep103tc2Pins& pins) {
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << pins.ckv) | (1ULL << pins.le) | (1ULL << pins.spv);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) return ERROR_RESOURCE;
    gpio_set_level(pins.ckv, 0);
    gpio_set_level(pins.le, 0);
    gpio_set_level(pins.spv, 0);
    return ERROR_NONE;
}

// Gives up on a row whose completion event never arrived and counts it as an error
static void IRAM_ATTR wait_for_row(Gdep103tc2Bus* bus) {
    const int64_t deadline_us = esp_timer_get_time() + ROW_TIMEOUT_US;
    while (!bus->dma_done) {
        if (esp_timer_get_time() > deadline_us) {
            bus->dma_done = true;
            bus->errors = bus->errors + 1;
            return;
        }
    }
}

error_t gdep103tc2_bus_init(Gdep103tc2Bus* bus, const Gdep103tc2Pins& pins, uint32_t pixel_clock_hz) {
    bus->pins = pins;
    bus->pixel_clock_hz = pixel_clock_hz;

    error_t error = configure_control_pins(pins);
    if (error != ERROR_NONE) return error;

    esp_lcd_i80_bus_config_t bus_config = {};
    bus_config.dc_gpio_num = pins.sph;
    bus_config.wr_gpio_num = pins.wr;
    bus_config.clk_src = LCD_CLK_SRC_PLL160M;
    for (int i = 0; i < BUS_WIDTH; i++) {
        bus_config.data_gpio_nums[i] = pins.data[i];
    }
    bus_config.bus_width = BUS_WIDTH;
    bus_config.max_transfer_bytes = ROW_BYTES;
    bus_config.dma_burst_size = DMA_BURST_BYTES;
    esp_err_t result = esp_lcd_new_i80_bus(&bus_config, &bus->bus);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to create the parallel bus: %s", esp_err_to_name(result));
        bus->bus = nullptr;
        return ERROR_RESOURCE;
    }

    esp_lcd_panel_io_i80_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_NC;
    io_config.pclk_hz = pixel_clock_hz;
    io_config.trans_queue_depth = TRANSACTION_QUEUE_DEPTH;
    io_config.on_color_trans_done = on_row_sent;
    io_config.user_ctx = bus;
    io_config.lcd_cmd_bits = ROW_COMMAND_BITS;
    io_config.lcd_param_bits = ROW_COMMAND_BITS;
    io_config.dc_levels.dc_idle_level = 1;
    io_config.dc_levels.dc_cmd_level = 1;
    io_config.dc_levels.dc_dummy_level = 1;
    io_config.dc_levels.dc_data_level = 0;
    result = esp_lcd_new_panel_io_i80(bus->bus, &io_config, &bus->io);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to create the panel IO: %s", esp_err_to_name(result));
        esp_lcd_del_i80_bus(bus->bus);
        bus->bus = nullptr;
        bus->io = nullptr;
        return ERROR_RESOURCE;
    }

    bus->dma_done = true;
    bus->errors = 0;
    return ERROR_NONE;
}

void gdep103tc2_bus_deinit(Gdep103tc2Bus* bus) {
    if (bus->io != nullptr) esp_lcd_panel_io_del(bus->io);
    if (bus->bus != nullptr) esp_lcd_del_i80_bus(bus->bus);
    bus->io = nullptr;
    bus->bus = nullptr;
    gpio_reset_pin(bus->pins.ckv);
    gpio_reset_pin(bus->pins.le);
    gpio_reset_pin(bus->pins.spv);
}

// Gate driver start sequence
void IRAM_ATTR gdep103tc2_bus_begin_pass(Gdep103tc2Bus* bus) {
    const Gdep103tc2Pins& pins = bus->pins;
    gpio_set_level(pins.ckv, 1);
    esp_rom_delay_us(START_CKV_HIGH_US);
    gpio_set_level(pins.spv, 0);
    esp_rom_delay_us(START_SPV_LOW_US);
    gpio_set_level(pins.ckv, 0);
    gpio_set_level(pins.ckv, 1);
    esp_rom_delay_us(START_SPV_RISE_US);
    gpio_set_level(pins.spv, 1);
    esp_rom_delay_us(START_PULSE_US);
    for (int i = 0; i < START_EXTRA_CKV_PULSES; i++) {
        gpio_set_level(pins.ckv, 0);
        gpio_set_level(pins.ckv, 1);
        if (i < START_EXTRA_CKV_PULSES - 1) esp_rom_delay_us(START_PULSE_US);
    }
}

void IRAM_ATTR gdep103tc2_bus_send_row(Gdep103tc2Bus* bus, const uint8_t* row, bool step, bool drive) {
    wait_for_row(bus);

    if (step) {
        while (esp_timer_get_time() - bus->row_selected_us < bus->row_dwell_us) {}
        gpio_set_level(bus->pins.ckv, 0);
        gpio_set_level(bus->pins.le, 1);
        gpio_set_level(bus->pins.le, 0);
    }
    gpio_set_level(bus->pins.ckv, 1);
    bus->row_selected_us = esp_timer_get_time();
    bus->row_dwell_us = drive ? ROW_DRIVE_DWELL_US : 0;

    bus->dma_done = false;
    if (esp_lcd_panel_io_tx_color(bus->io, ROW_COMMAND, row, ROW_BYTES) != ESP_OK) {
        bus->dma_done = true;
        bus->errors = bus->errors + 1;
    }
}

void IRAM_ATTR gdep103tc2_bus_end_pass(Gdep103tc2Bus* bus) {
    wait_for_row(bus);
    while (esp_timer_get_time() - bus->row_selected_us < bus->row_dwell_us) {}
    bus->row_dwell_us = 0;
    esp_rom_delay_us(PASS_END_DELAY_US);
}

void IRAM_ATTR gdep103tc2_bus_power_down(Gdep103tc2Bus* bus) {
    gpio_set_level(bus->pins.ckv, 0);
    gpio_set_level(bus->pins.le, 0);
    gpio_set_level(bus->pins.spv, 0);
    bus->row_dwell_us = 0;
}

uint32_t gdep103tc2_bus_take_errors(Gdep103tc2Bus* bus) {
    const uint32_t errors = bus->errors;
    bus->errors = 0;
    return errors;
}
