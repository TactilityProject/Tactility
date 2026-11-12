// SPDX-License-Identifier: Apache-2.0
#include <drivers/ssd1677.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>

#define TAG "Ssd1677"
#define GET_CONFIG(device) (static_cast<const Ssd1677Config*>((device)->config))

// The panel is driven with waveforms of the driver instead of the ones in the OTP of the controller. Only pixels whose
// target shade changes are driven, so nothing flashes. Black is driven dark. White and the grays are driven light at
// the same time, which erases what the pixel showed before, and the grays then get a dose of dark frames for their
// shade.
//
// The controller picks one of 4 waveforms per pixel from the bit of the pixel in the red RAM (high bit) and in the
// black and white RAM (low bit). Waveform 0 holds, so a pass has 3 waveforms: an update runs a pass for black and the
// light frames and up to 2 passes for 3 grays each. Frame counts and doses were measured with a camera.

static constexpr int PANEL_WIDTH = 800;
static constexpr int PANEL_HEIGHT = 480;
static constexpr size_t PIXEL_COUNT = static_cast<size_t>(PANEL_WIDTH) * PANEL_HEIGHT;
static constexpr size_t PLANE_ROW_BYTES = PANEL_WIDTH / 8;
static constexpr size_t PLANE_SIZE = PLANE_ROW_BYTES * PANEL_HEIGHT;

// Source voltages of a waveform phase
static constexpr uint8_t HOLD = 0b00;
static constexpr uint8_t DARK = 0b01;
static constexpr uint8_t LIGHT = 0b10;

// VSH1 15 V, VSH2 3.2 V (not used), VSL -15 V. The doses were measured with these.
static constexpr uint8_t SOURCE_VOLTAGES[3] = { 0x41, 0x96, 0x32 };

// Shades from black to white. Each gray is drawn by one waveform of one of the gray passes.
static constexpr int SHADES = 8;
static constexpr uint8_t SHADE_BLACK = 0;
static constexpr uint8_t SHADE_WHITE = SHADES - 1;
static constexpr int GRAY_PASSES = 2;
static constexpr int PASSES = GRAY_PASSES + 1;

struct GrayDose {
    // Gray pass and waveform 1 to 3 in it
    uint8_t pass;
    uint8_t waveform;
    uint8_t voltage;
    uint8_t frames;
};

static constexpr GrayDose SHADE_DOSE[SHADES] = {
    { 0, 0, HOLD, 0 },
    { 1, 3, DARK, 17 },
    { 1, 2, DARK, 14 },
    { 1, 1, DARK, 12 },
    { 0, 3, DARK, 10 },
    { 0, 2, DARK, 7 },
    { 0, 1, DARK, 4 },
    { 0, 0, HOLD, 0 },
};
// Brightness of each shade as measured, 0 is black and 255 is white
static constexpr uint8_t SHADE_BRIGHTNESS[SHADES] = { 0, 40, 79, 103, 142, 185, 220, 255 };

static constexpr uint8_t LIGHT_FRAMES = 30;
static constexpr uint8_t BLACK_FRAMES = 30;
// A gray that replaces a shade up to DARK_SOURCE_SHADE gets EXTRA_LIGHT_FRAMES more light frames, else the dark shade
// stays visible in it
static constexpr uint8_t EXTRA_LIGHT_FRAMES = 30;
static constexpr uint8_t DARK_SOURCE_SHADE = 2;
static constexpr uint8_t FLASH_DARK_FRAMES = 40;
static constexpr uint8_t FLASH_LIGHT_FRAMES = 40;
static constexpr uint8_t END_HOLD_FRAMES = 2;

// Frame rate setting of the waveform groups, 0x88 gives frames of 5.8 ms. A nibble of 0x0 to 0x8 gave 67.6, 40.8,
// 20.8, 14.2, 10.8, 8.8, 7.5, 6.5 and 5.8 ms. Shorter frames drive harder for the same time.
static constexpr uint8_t FRAME_RATE = 0x88;

// Bytes of the waveform written with register 0x32: voltages of LUT0 to LUT4 per group, phase lengths and repeats
// per group, frame rates
static constexpr size_t LUT_SIZE = 105;
static constexpr size_t LUT_TIMING_OFFSET = 50;
static constexpr size_t LUT_FRAME_RATE_OFFSET = 100;
static constexpr int LUT_PHASES = 40;

// Display update sequences of register 0x22
// Clock on, load the waveform of the OTP for the written temperature, clock off. This also sets the voltages.
static constexpr uint8_t SEQUENCE_LOAD_OTP = 0xB1;
// Clock and analog on, display mode 1 with the written waveform, keeps the analog part on
static constexpr uint8_t SEQUENCE_CUSTOM = 0xC4;
// As SEQUENCE_CUSTOM while the analog part is on
static constexpr uint8_t SEQUENCE_CUSTOM_POWERED = 0x84;
// Analog and clock off
static constexpr uint8_t SEQUENCE_POWER_DOWN = 0x83;

// Temperature that selects the OTP voltages, 25 degrees
static constexpr uint8_t ROOM_TEMPERATURE = 0x19;

static constexpr uint8_t BORDER_HOLD = 0x80;

static constexpr size_t SPI_CHUNK_BYTES = 4096;
static constexpr uint32_t BUSY_TIMEOUT_MS = 20000;
static constexpr uint32_t COALESCE_MS = 30;
static constexpr uint32_t IDLE_POWER_DOWN_MS = 3000;
static constexpr uint32_t RETRY_DELAY_MS = 1000;

// Pixels that rested for minutes keep faint traces of what they showed, which only a flash removes. Tactility calls
// refresh() after the user stopped using the device. Content that changes without input gets a refresh from the
// driver once it was quiet for AUTO_REFRESH_QUIET_MS, at most every MINIMAL_AUTO_REFRESH_INTERVAL_MS.
static constexpr int64_t AUTO_REFRESH_QUIET_MS = 5000;
static constexpr int64_t MINIMAL_AUTO_REFRESH_INTERVAL_MS = 10 * 60 * 1000;

static constexpr uint32_t CMD_UPDATE = 1U << 0;
static constexpr uint32_t CMD_CLEAR = 1U << 1;
static constexpr uint32_t CMD_REFRESH = 1U << 2;
static constexpr uint32_t CMD_SLEEP = 1U << 3;
static constexpr uint32_t CMD_WAKE = 1U << 4;
static constexpr uint32_t CMD_EXIT = 1U << 5;

// 4x4 ordered (Bayer) dither thresholds
static constexpr uint8_t BAYER_4X4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

// A phase of a waveform and the voltage of each of the 4 waveforms in it
struct Phase {
    uint8_t frames;
    uint8_t voltage[4];
};

// Columns [x0, x1) of rows [y0, y1) of the panel
struct Region {
    int x0;
    int y0;
    int x1;
    int y1;
};

static constexpr Region NO_REGION = { PANEL_WIDTH, PANEL_HEIGHT, 0, 0 };
static constexpr Region FULL_REGION = { 0, 0, PANEL_WIDTH, PANEL_HEIGHT };

// A luminance lies between two shades and is dithered between them
struct Tone {
    uint8_t darker;
    uint8_t lighter;
    // Share of the lighter shade, 0 to 16
    uint8_t share;
};

struct Ssd1677Internal {
    Device* spi_controller;
    spi_device_handle_t spi;
    int pin_dc;
    int pin_reset;
    int pin_busy;
    int pin_cs;
    uint8_t rotation;
    TaskHandle_t task;
    SemaphoreHandle_t request_mutex;
    SemaphoreHandle_t request_done;
    portMUX_TYPE lock;
    volatile bool exited;
    // Luminance of each pixel in native panel orientation
    uint8_t* luminance;
    // Area with changes not yet picked up by the task, guarded by lock
    Region dirty;
    // The remaining members are only used by the task
    // Shade that each pixel shows and the one it gets in the current update
    uint8_t* shown;
    uint8_t* target;
    // RAM planes of each pass that select the waveform of each pixel, 1 bit per pixel with the leftmost pixel in the
    // highest bit. Pass 0 is the light and black pass, 1 and 2 are the gray passes.
    uint8_t* plane_red[PASSES];
    uint8_t* plane_bw[PASSES];
    // Rows [plane_y0, plane_y1) of the planes and [ram_y0, ram_y1) of the controller RAM can hold bits that are not 0
    int plane_y0;
    int plane_y1;
    int ram_y0;
    int ram_y1;
    uint8_t lut[LUT_SIZE];
    Tone* tones;
    GrayDose doses[SHADES];
    uint8_t extra_light;
    uint8_t dark_source;
    // Bounce buffer for SPI transfers in internal DMA capable memory
    uint8_t* transfer;
    // Set when an SPI transfer failed or the panel stayed busy, cleared at the start of each pass
    bool update_failed;
    // The shown shades are unknown or the registers lost their values, so the next update flashes and draws everything
    bool needs_redraw;
    bool powered;
    bool asleep;
    bool changed_since_refresh;
    int64_t last_change_us;
    int64_t last_refresh_us;
};

static void build_tones(Tone* tones) {
    for (int luminance = 0; luminance < 256; luminance++) {
        int shade = 0;
        while (shade < SHADES - 2 && luminance > SHADE_BRIGHTNESS[shade + 1]) {
            shade++;
        }
        const int range = SHADE_BRIGHTNESS[shade + 1] - SHADE_BRIGHTNESS[shade];
        const int share = ((luminance - SHADE_BRIGHTNESS[shade]) * 16 + range / 2) / range;
        tones[luminance].darker = static_cast<uint8_t>(shade);
        tones[luminance].lighter = static_cast<uint8_t>(shade + 1);
        tones[luminance].share = static_cast<uint8_t>(std::clamp(share, 0, 16));
    }
}

// region SPI

static void write_bytes(Ssd1677Internal* internal, const uint8_t* data, size_t length) {
    while (length > 0 && !internal->update_failed) {
        const size_t chunk = std::min(length, SPI_CHUNK_BYTES);
        memcpy(internal->transfer, data, chunk);
        spi_transaction_t transaction = {};
        transaction.length = chunk * 8;
        transaction.tx_buffer = internal->transfer;
        if (spi_device_polling_transmit(internal->spi, &transaction) != ESP_OK) {
            LOG_E(TAG, "SPI transmit failed");
            internal->update_failed = true;
            return;
        }
        data += chunk;
        length -= chunk;
    }
}

static void write_command(Ssd1677Internal* internal, uint8_t command, const uint8_t* data, size_t length) {
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 0);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_dc), 0);
    write_bytes(internal, &command, 1);
    if (length > 0) {
        gpio_set_level(static_cast<gpio_num_t>(internal->pin_dc), 1);
        write_bytes(internal, data, length);
    }
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 1);
}

static void write_command(Ssd1677Internal* internal, uint8_t command, std::initializer_list<uint8_t> data = {}) {
    write_command(internal, command, data.begin(), data.size());
}

static void wait_busy(Ssd1677Internal* internal) {
    // The busy line rises shortly after a command
    vTaskDelay(pdMS_TO_TICKS(2));
    const TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(static_cast<gpio_num_t>(internal->pin_busy))) {
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(BUSY_TIMEOUT_MS)) {
            LOG_W(TAG, "Timeout waiting for panel");
            internal->update_failed = true;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// endregion

// region Panel

// Sets the RAM window to rows [y0, y1) of the panel. The RAM address of panel row y is PANEL_HEIGHT - 1 - y, the
// address counter counts down.
static void set_window(Ssd1677Internal* internal, int y0, int y1) {
    constexpr uint8_t last_column_low = (PANEL_WIDTH - 1) & 0xFF;
    constexpr uint8_t last_column_high = (PANEL_WIDTH - 1) >> 8;
    const int first = PANEL_HEIGHT - 1 - y0;
    const int last = PANEL_HEIGHT - y1;
    const uint8_t first_low = first & 0xFF;
    const uint8_t first_high = first >> 8;
    write_command(internal, 0x44, { 0x00, 0x00, last_column_low, last_column_high });
    write_command(internal, 0x45, { first_low, first_high, static_cast<uint8_t>(last & 0xFF), static_cast<uint8_t>(last >> 8) });
    write_command(internal, 0x4E, { 0x00, 0x00 });
    write_command(internal, 0x4F, { first_low, first_high });
}

static void hardware_init(Ssd1677Internal* internal) {
    // A reset turns the analog part off
    internal->powered = false;
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_reset), 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_reset), 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    wait_busy(internal);

    write_command(internal, 0x12);
    wait_busy(internal);

    // Booster soft start
    write_command(internal, 0x0C, { 0xAE, 0xC7, 0xC3, 0xC0, 0x80 });
    // Driver output control: gate lines and scan order
    write_command(internal, 0x01, { (PANEL_HEIGHT - 1) & 0xFF, (PANEL_HEIGHT - 1) >> 8, 0x02 });
    write_command(internal, 0x3C, { BORDER_HOLD });
    // Data entry mode: X increments, Y decrements
    write_command(internal, 0x11, { 0x01 });
    set_window(internal, 0, PANEL_HEIGHT);
    wait_busy(internal);
    // The content of the RAM is not known after a reset
    internal->ram_y0 = 0;
    internal->ram_y1 = PANEL_HEIGHT;

    // The gate and VCOM voltages of the panel come with the OTP waveform
    write_command(internal, 0x1A, { ROOM_TEMPERATURE });
    write_command(internal, 0x22, { SEQUENCE_LOAD_OTP });
    write_command(internal, 0x20);
    wait_busy(internal);
    write_command(internal, 0x04, SOURCE_VOLTAGES, sizeof(SOURCE_VOLTAGES));
}

static void build_lut(uint8_t* lut, const Phase* phases, int count, uint8_t frame_rate = FRAME_RATE) {
    memset(lut, 0, LUT_SIZE);
    for (int i = 0; i < count && i < LUT_PHASES; i++) {
        const int group = i / 4;
        const int shift = 6 - 2 * (i % 4);
        for (int waveform = 0; waveform < 4; waveform++) {
            lut[waveform * 10 + group] |= static_cast<uint8_t>(phases[i].voltage[waveform] << shift);
        }
        lut[LUT_TIMING_OFFSET + group * 5 + i % 4] = phases[i].frames;
    }
    memset(lut + LUT_FRAME_RATE_OFFSET, frame_rate, LUT_SIZE - LUT_FRAME_RATE_OFFSET);
}

// Runs the waveform on the panel with the planes of the pass, which are 0 outside rows [y0, y1). A pass below 0
// leaves the RAM as it is, for waveforms that are the same for every pixel. Returns false when a transfer failed or
// the panel stayed busy.
static bool run_pass(Ssd1677Internal* internal, int pass, int y0, int y1, const Phase* phases, int count, uint8_t frame_rate = FRAME_RATE) {
    // The voltages of the last phase stay on the panel after the pass, so it ends with a phase that holds
    Phase all[LUT_PHASES];
    count = std::min(count, LUT_PHASES - 1);
    memcpy(all, phases, count * sizeof(Phase));
    all[count++] = { END_HOLD_FRAMES, { HOLD, HOLD, HOLD, HOLD } };
    build_lut(internal->lut, all, count, frame_rate);
    internal->update_failed = false;
    spi_controller_lock(internal->spi_controller);
    if (internal->needs_redraw) {
        hardware_init(internal);
    }
    write_command(internal, 0x32, internal->lut, LUT_SIZE);
    if (pass >= 0) {
        // Rows that may still hold bits of an earlier pass are written too
        const int w0 = std::min(y0, internal->ram_y0);
        const int w1 = std::max(y1, internal->ram_y1);
        if (w0 < w1) {
            const size_t offset = static_cast<size_t>(w0) * PLANE_ROW_BYTES;
            const size_t size = static_cast<size_t>(w1 - w0) * PLANE_ROW_BYTES;
            set_window(internal, w0, w1);
            write_command(internal, 0x26, internal->plane_red[pass] + offset, size);
            set_window(internal, w0, w1);
            write_command(internal, 0x24, internal->plane_bw[pass] + offset, size);
        }
        internal->ram_y0 = y0;
        internal->ram_y1 = y1;
    }
    if (internal->update_failed) {
        spi_controller_unlock(internal->spi_controller);
        return false;
    }
    write_command(internal, 0x22, { internal->powered ? SEQUENCE_CUSTOM_POWERED : SEQUENCE_CUSTOM });
    write_command(internal, 0x20);
    spi_controller_unlock(internal->spi_controller);
    internal->powered = true;
    wait_busy(internal);
    return !internal->update_failed;
}

static void power_down(Ssd1677Internal* internal) {
    if (!internal->powered) return;
    spi_controller_lock(internal->spi_controller);
    write_command(internal, 0x22, { SEQUENCE_POWER_DOWN });
    write_command(internal, 0x20);
    spi_controller_unlock(internal->spi_controller);
    wait_busy(internal);
    internal->powered = false;
}

// Takes the area with changes. Returns false when there are none.
static bool take_dirty(Ssd1677Internal* internal, Region* region) {
    portENTER_CRITICAL(&internal->lock);
    *region = internal->dirty;
    internal->dirty = NO_REGION;
    portEXIT_CRITICAL(&internal->lock);
    return region->y0 < region->y1;
}

static void mark_all_dirty(Ssd1677Internal* internal) {
    portENTER_CRITICAL(&internal->lock);
    internal->dirty = FULL_REGION;
    portEXIT_CRITICAL(&internal->lock);
}

// Builds the phases of a gray pass: each waveform drives from the start for the frames of its dose
static int build_gray_phases(const GrayDose* shade_doses, int pass, Phase* phases) {
    const GrayDose* doses[4] = {};
    for (int shade = 0; shade < SHADES; shade++) {
        const GrayDose& dose = shade_doses[shade];
        if (dose.frames > 0 && dose.pass == pass) {
            doses[dose.waveform] = &dose;
        }
    }
    int count = 0;
    int elapsed = 0;
    while (true) {
        int next = 256;
        for (const GrayDose* dose : doses) {
            if (dose != nullptr && dose->frames > elapsed) next = std::min<int>(next, dose->frames);
        }
        if (next == 256) break;
        Phase& phase = phases[count++];
        phase.frames = static_cast<uint8_t>(next - elapsed);
        for (int waveform = 0; waveform < 4; waveform++) {
            const GrayDose* dose = doses[waveform];
            phase.voltage[waveform] = dose != nullptr && dose->frames > elapsed ? dose->voltage : HOLD;
        }
        elapsed = next;
    }
    return count;
}

// What the planes of an update hold
struct Planned {
    bool changed;
    bool black;
    bool extra;
    bool gray[GRAY_PASSES];
};

// Computes the target shades of the region and the planes of all passes in one sweep. Pass 0: waveform 1 drives
// light, 2 drives dark. The gray passes: the waveform of the dose of the target.
static Planned plan_changes(Ssd1677Internal* internal, const Region& region) {
    const int y0 = region.y0;
    const int y1 = region.y1;
    const size_t byte0 = region.x0 / 8;
    const size_t byte1 = (region.x1 + 7) / 8;
    for (int pass = 0; pass < PASSES; pass++) {
        for (int y = internal->plane_y0; y < internal->plane_y1; y++) {
            memset(internal->plane_red[pass] + y * PLANE_ROW_BYTES, 0, PLANE_ROW_BYTES);
            memset(internal->plane_bw[pass] + y * PLANE_ROW_BYTES, 0, PLANE_ROW_BYTES);
        }
    }
    internal->plane_y0 = y0;
    internal->plane_y1 = y1;

    // Waveform of each pass per shade, for pixels whose shade changes
    uint8_t waveforms[SHADES][PASSES];
    for (int shade = 0; shade < SHADES; shade++) {
        const GrayDose& dose = internal->doses[shade];
        waveforms[shade][0] = shade == SHADE_BLACK ? 2 : 1;
        for (int pass = 0; pass < GRAY_PASSES; pass++) {
            waveforms[shade][pass + 1] = dose.frames > 0 && dose.pass == pass ? dose.waveform : 0;
        }
    }

    uint8_t used[PASSES] = {};
    for (int y = y0; y < y1; y++) {
        const uint8_t* luminance = internal->luminance + y * PANEL_WIDTH;
        uint8_t* target = internal->target + y * PANEL_WIDTH;
        const uint8_t* shown = internal->shown + y * PANEL_WIDTH;
        const uint8_t* thresholds = BAYER_4X4[y & 3];
        const size_t row = static_cast<size_t>(y) * PLANE_ROW_BYTES;
        for (size_t byte = byte0; byte < byte1; byte++) {
            uint8_t red[PASSES] = {};
            uint8_t bw[PASSES] = {};
            for (int bit = 0; bit < 8; bit++) {
                const int x = static_cast<int>(byte * 8) + bit;
                const Tone& tone = internal->tones[luminance[x]];
                const uint8_t shade = tone.share > thresholds[x & 3] ? tone.lighter : tone.darker;
                target[x] = shade;
                const uint8_t mask = static_cast<uint8_t>(0x80 >> bit);
                if (shade != shown[x]) {
                    // Waveform 3 of pass 0 erases with extra light frames
                    if (shade != SHADE_BLACK && shade != SHADE_WHITE && shown[x] <= internal->dark_source && internal->extra_light > 0) {
                        red[0] |= mask;
                        bw[0] |= mask;
                        for (int pass = 1; pass < PASSES; pass++) {
                            const uint8_t waveform = waveforms[shade][pass];
                            red[pass] |= (waveform & 2) ? mask : 0;
                            bw[pass] |= (waveform & 1) ? mask : 0;
                        }
                        continue;
                    }
                    for (int pass = 0; pass < PASSES; pass++) {
                        const uint8_t waveform = waveforms[shade][pass];
                        red[pass] |= (waveform & 2) ? mask : 0;
                        bw[pass] |= (waveform & 1) ? mask : 0;
                    }
                }
            }
            for (int pass = 0; pass < PASSES; pass++) {
                internal->plane_red[pass][row + byte] = red[pass];
                internal->plane_bw[pass][row + byte] = bw[pass];
                used[pass] |= red[pass] | bw[pass];
            }
        }
    }

    Planned planned = {};
    planned.changed = used[0] != 0;
    for (int y = y0; y < y1 && !(planned.black && planned.extra); y++) {
        const uint8_t* red = internal->plane_red[0] + y * PLANE_ROW_BYTES;
        const uint8_t* bw = internal->plane_bw[0] + y * PLANE_ROW_BYTES;
        for (size_t byte = byte0; byte < byte1; byte++) {
            planned.black |= (red[byte] & ~bw[byte]) != 0;
            planned.extra |= (red[byte] & bw[byte]) != 0;
        }
    }
    for (int pass = 0; pass < GRAY_PASSES; pass++) {
        planned.gray[pass] = used[pass + 1] != 0;
    }
    return planned;
}

// Draws the shades of the region whose target changed
static bool draw_changes(Ssd1677Internal* internal, const Region& region) {
    const int64_t start_us = esp_timer_get_time();
    const int y0 = region.y0;
    const int y1 = region.y1;
    const Planned planned = plan_changes(internal, region);
    if (!planned.changed) {
        return true;
    }

    // Light, black and the extra light run at the same time
    const GrayDose erase_doses[SHADES] = {
        { 0, 1, LIGHT, LIGHT_FRAMES },
        { 0, 2, DARK, static_cast<uint8_t>(planned.black ? BLACK_FRAMES : 0) },
        { 0, 3, LIGHT, static_cast<uint8_t>(planned.extra ? LIGHT_FRAMES + internal->extra_light : 0) },
    };
    Phase erase[8];
    const int erase_count = build_gray_phases(erase_doses, 0, erase);
    bool ok = run_pass(internal, 0, y0, y1, erase, erase_count);
    int passes = 1;

    // A gray pass runs when it or a later one has pixels, so each gray always gets the same timing
    bool needed[GRAY_PASSES];
    for (int pass = 0; pass < GRAY_PASSES; pass++) {
        needed[pass] = planned.gray[pass];
    }
    for (int pass = GRAY_PASSES - 2; pass >= 0; pass--) {
        needed[pass] |= needed[pass + 1];
    }
    for (int pass = 0; pass < GRAY_PASSES && ok && needed[pass]; pass++) {
        Phase phases[8];
        const int count = build_gray_phases(internal->doses, pass, phases);
        ok = run_pass(internal, pass + 1, y0, y1, phases, count);
        passes++;
    }

    const int x0 = region.x0 & ~7;
    const int x1 = std::min((region.x1 + 7) & ~7, PANEL_WIDTH);
    for (int y = y0; y < y1; y++) {
        memcpy(internal->shown + y * PANEL_WIDTH + x0, internal->target + y * PANEL_WIDTH + x0, x1 - x0);
    }
    internal->changed_since_refresh = true;
    internal->last_change_us = esp_timer_get_time();
    LOG_D(TAG, "Columns %d to %d of rows %d to %d in %d passes took %d ms", region.x0, region.x1, y0, y1, passes,
        static_cast<int>((internal->last_change_us - start_us) / 1000));
    return ok;
}

// Drives the whole panel dark and then light, which leaves it white without ghosting
static bool flash(Ssd1677Internal* internal) {
    const int64_t start_us = esp_timer_get_time();
    const Phase phases[] = {
        { FLASH_DARK_FRAMES, { DARK, DARK, DARK, DARK } },
        { FLASH_LIGHT_FRAMES, { LIGHT, LIGHT, LIGHT, LIGHT } },
    };
    const bool ok = run_pass(internal, -1, 0, 0, phases, 2);
    memset(internal->shown, SHADE_WHITE, PIXEL_COUNT);
    internal->needs_redraw = !ok;
    internal->changed_since_refresh = false;
    internal->last_refresh_us = esp_timer_get_time();
    LOG_D(TAG, "Flash took %d ms", static_cast<int>((internal->last_refresh_us - start_us) / 1000));
    return ok;
}

// Flashes the panel and draws the content again
static bool refresh(Ssd1677Internal* internal) {
    if (!flash(internal)) {
        return false;
    }
    Region ignored;
    take_dirty(internal, &ignored);
    const bool ok = draw_changes(internal, FULL_REGION);
    internal->changed_since_refresh = false;
    internal->needs_redraw = !ok;
    return ok;
}

static void enter_sleep(Ssd1677Internal* internal) {
    if (internal->asleep) return;
    power_down(internal);
    spi_controller_lock(internal->spi_controller);
    write_command(internal, 0x10, { 0x01 });
    spi_controller_unlock(internal->spi_controller);
    internal->asleep = true;
}

static void leave_sleep(Ssd1677Internal* internal) {
    if (!internal->asleep) return;
    internal->asleep = false;
    // Only a reset wakes the controller, which loses its registers
    internal->needs_redraw = true;
}

static bool auto_refresh_due(const Ssd1677Internal* internal, int64_t now_us) {
    return internal->changed_since_refresh &&
        now_us - internal->last_change_us >= AUTO_REFRESH_QUIET_MS * 1000 &&
        now_us - internal->last_refresh_us >= MINIMAL_AUTO_REFRESH_INTERVAL_MS * 1000;
}

static TickType_t wait_time(const Ssd1677Internal* internal) {
    if (internal->asleep) return portMAX_DELAY;
    if (internal->needs_redraw) return pdMS_TO_TICKS(RETRY_DELAY_MS);
    if (internal->powered) return pdMS_TO_TICKS(IDLE_POWER_DOWN_MS);
    if (!internal->changed_since_refresh) return portMAX_DELAY;
    const int64_t now_us = esp_timer_get_time();
    const int64_t quiet_left_us = internal->last_change_us + AUTO_REFRESH_QUIET_MS * 1000 - now_us;
    const int64_t interval_left_us = internal->last_refresh_us + MINIMAL_AUTO_REFRESH_INTERVAL_MS * 1000 - now_us;
    const int64_t left_ms = std::max<int64_t>(std::max(quiet_left_us, interval_left_us) / 1000, 1);
    return pdMS_TO_TICKS(left_ms);
}

static void update_task(void* arg) {
    auto* internal = static_cast<Ssd1677Internal*>(arg);

    while (true) {
        uint32_t commands = 0;
        const bool notified = xTaskNotifyWait(0, UINT32_MAX, &commands, wait_time(internal)) == pdTRUE;

        if (commands & CMD_EXIT) {
            // The panel keeps its image in deep sleep
            enter_sleep(internal);
            break;
        }

        if (commands & CMD_SLEEP) {
            enter_sleep(internal);
        }

        if (commands & CMD_WAKE) {
            leave_sleep(internal);
        }

        if (commands & CMD_CLEAR) {
            if (!internal->asleep) {
                Region ignored;
                take_dirty(internal, &ignored);
                flash(internal);
            }
            xSemaphoreGive(internal->request_done);
        }

        if (internal->asleep) {
            continue;
        }

        if ((commands & CMD_REFRESH) || internal->needs_redraw) {
            if (commands & CMD_UPDATE) {
                vTaskDelay(pdMS_TO_TICKS(COALESCE_MS));
            }
            if (!refresh(internal)) {
                LOG_E(TAG, "Refresh failed, trying again");
            }
            continue;
        }

        if (commands & CMD_UPDATE) {
            // Tiles of one redraw arrive within a few milliseconds
            vTaskDelay(pdMS_TO_TICKS(COALESCE_MS));
        }

        Region region;
        if (take_dirty(internal, &region)) {
            if (!draw_changes(internal, region)) {
                LOG_E(TAG, "Update failed, redrawing the screen");
                internal->needs_redraw = true;
                mark_all_dirty(internal);
            }
        } else if (!notified) {
            power_down(internal);
            if (auto_refresh_due(internal, esp_timer_get_time())) {
                refresh(internal);
            }
        }
    }

    internal->exited = true;
    vTaskDelete(nullptr);
}

// endregion

// region Helpers

// Display coordinates to native panel coordinates. The display is the panel turned
// counter-clockwise by the rotation, like LV_DISPLAY_ROTATION_*.
static inline void to_native(uint8_t rotation, int u, int v, int* x, int* y) {
    switch (rotation) {
        case 1:
            *x = v;
            *y = PANEL_HEIGHT - 1 - u;
            break;
        case 2:
            *x = PANEL_WIDTH - 1 - u;
            *y = PANEL_HEIGHT - 1 - v;
            break;
        case 3:
            *x = PANEL_WIDTH - 1 - v;
            *y = u;
            break;
        default:
            *x = u;
            *y = v;
            break;
    }
}

static bool swaps_axes(uint8_t rotation) {
    return rotation == 1 || rotation == 3;
}

static void notify_task(Ssd1677Internal* internal, uint32_t command) {
    xTaskNotify(internal->task, command, eSetBits);
}

static error_t clear_blocking(Ssd1677Internal* internal) {
    xSemaphoreTake(internal->request_mutex, portMAX_DELAY);
    notify_task(internal, CMD_CLEAR);
    xSemaphoreTake(internal->request_done, portMAX_DELAY);
    xSemaphoreGive(internal->request_mutex);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t ssd1677_reset(Device* device) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    notify_task(internal, CMD_SLEEP | CMD_WAKE);
    return ERROR_NONE;
}

static error_t ssd1677_init(Device*) {
    return ERROR_NONE;
}

// Reports GRAYSCALE8 so LVGL renders partial tiles
static error_t ssd1677_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    const int32_t width = x_end - x_start;
    const auto* source = static_cast<const uint8_t*>(color_data);

    if (internal->rotation == 0) {
        for (int32_t v = y_start; v < y_end; v++) {
            memcpy(internal->luminance + v * PANEL_WIDTH + x_start, source + (v - y_start) * width, width);
        }
    } else {
        for (int32_t v = y_start; v < y_end; v++) {
            const uint8_t* source_row = source + (v - y_start) * width;
            for (int32_t u = x_start; u < x_end; u++) {
                int x, y;
                to_native(internal->rotation, u, v, &x, &y);
                internal->luminance[y * PANEL_WIDTH + x] = source_row[u - x_start];
            }
        }
    }

    int x0, ya, x1, yb;
    to_native(internal->rotation, x_start, y_start, &x0, &ya);
    to_native(internal->rotation, x_end - 1, y_end - 1, &x1, &yb);
    portENTER_CRITICAL(&internal->lock);
    Region& dirty = internal->dirty;
    dirty.x0 = std::min({ dirty.x0, x0, x1 });
    dirty.x1 = std::max({ dirty.x1, x0 + 1, x1 + 1 });
    dirty.y0 = std::min({ dirty.y0, ya, yb });
    dirty.y1 = std::max({ dirty.y1, ya + 1, yb + 1 });
    portEXIT_CRITICAL(&internal->lock);
    notify_task(internal, CMD_UPDATE);
    return ERROR_NONE;
}

/** Flashes the panel to white */
static error_t ssd1677_clear(Device* device) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    memset(internal->luminance, 0xFF, PIXEL_COUNT);
    return clear_blocking(internal);
}

/** Flashes the panel and draws the content again, which removes all ghosting. Does not wait for the panel. */
static error_t ssd1677_refresh(Device* device) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    notify_task(internal, CMD_REFRESH);
    return ERROR_NONE;
}

static error_t ssd1677_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    notify_task(internal, on_off ? CMD_WAKE : CMD_SLEEP);
    return ERROR_NONE;
}

static DisplayColorFormat ssd1677_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

static uint16_t ssd1677_get_resolution_x(Device* device) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    return swaps_axes(internal->rotation) ? PANEL_HEIGHT : PANEL_WIDTH;
}

static uint16_t ssd1677_get_resolution_y(Device* device) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));
    return swaps_axes(internal->rotation) ? PANEL_WIDTH : PANEL_HEIGHT;
}

// endregion

static const DisplayApi ssd1677_display_api = {
    // draw_bitmap() converts into its own buffer and never DMAs from the given pointer
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
    .reset = ssd1677_reset,
    .init = ssd1677_init,
    .draw_bitmap = ssd1677_draw_bitmap,
    .clear = ssd1677_clear,
    .refresh = ssd1677_refresh,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = ssd1677_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = ssd1677_get_color_format,
    .get_resolution_x = ssd1677_get_resolution_x,
    .get_resolution_y = ssd1677_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

// region Driver lifecycle

static void configure_gpio(const Ssd1677Internal* internal) {
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << internal->pin_dc) | (1ULL << internal->pin_cs) | (1ULL << internal->pin_reset),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&outputs);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 1);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_reset), 1);

    gpio_config_t input = {
        .pin_bit_mask = 1ULL << internal->pin_busy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&input);
}

static void destroy(Ssd1677Internal* internal) {
    if (internal->spi != nullptr) spi_bus_remove_device(internal->spi);
    if (internal->request_mutex != nullptr) vSemaphoreDelete(internal->request_mutex);
    if (internal->request_done != nullptr) vSemaphoreDelete(internal->request_done);
    memory_free(internal->luminance);
    memory_free(internal->shown);
    memory_free(internal->target);
    for (int pass = 0; pass < PASSES; pass++) {
        memory_free(internal->plane_red[pass]);
        memory_free(internal->plane_bw[pass]);
    }
    memory_free(internal->tones);
    heap_caps_free(internal->transfer);
    memory_free(internal);
}

static error_t start(Device* device) {
    Device* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    if (config->clock_speed_hz <= 0 || config->rotation > 3) {
        LOG_E(TAG, "Invalid clock speed or rotation");
        return ERROR_NOT_SUPPORTED;
    }

    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    static const MemoryPolicy external_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 16 };
    auto* internal = static_cast<Ssd1677Internal*>(memory_calloc_with_policy(1, sizeof(Ssd1677Internal), &internal_policy));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->spi_controller = parent;
    internal->pin_dc = config->pin_dc.pin;
    internal->pin_reset = config->pin_reset.pin;
    internal->pin_busy = config->pin_busy.pin;
    internal->pin_cs = config->pin_cs.pin;
    internal->rotation = config->rotation;
    internal->lock = portMUX_INITIALIZER_UNLOCKED;
    internal->dirty = NO_REGION;
    internal->request_mutex = xSemaphoreCreateMutex();
    internal->request_done = xSemaphoreCreateBinary();
    internal->luminance = static_cast<uint8_t*>(memory_alloc_with_policy(PIXEL_COUNT, &external_policy));
    internal->shown = static_cast<uint8_t*>(memory_alloc_with_policy(PIXEL_COUNT, &external_policy));
    internal->target = static_cast<uint8_t*>(memory_alloc_with_policy(PIXEL_COUNT, &external_policy));
    bool planes_allocated = true;
    for (int pass = 0; pass < PASSES; pass++) {
        internal->plane_red[pass] = static_cast<uint8_t*>(memory_calloc_with_policy(1, PLANE_SIZE, &external_policy));
        internal->plane_bw[pass] = static_cast<uint8_t*>(memory_calloc_with_policy(1, PLANE_SIZE, &external_policy));
        planes_allocated &= internal->plane_red[pass] != nullptr && internal->plane_bw[pass] != nullptr;
    }
    // Read for every pixel
    internal->tones = static_cast<Tone*>(memory_alloc_with_policy(256 * sizeof(Tone), &internal_policy));
    internal->transfer = static_cast<uint8_t*>(heap_caps_malloc(SPI_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!internal->request_mutex || !internal->request_done || !internal->luminance || !internal->shown || !internal->target ||
        !planes_allocated || !internal->tones || !internal->transfer) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    memset(internal->luminance, 0xFF, PIXEL_COUNT);
    memset(internal->shown, SHADE_WHITE, PIXEL_COUNT);
    build_tones(internal->tones);
    memcpy(internal->doses, SHADE_DOSE, sizeof(SHADE_DOSE));
    internal->extra_light = EXTRA_LIGHT_FRAMES;
    internal->dark_source = DARK_SOURCE_SHADE;

    configure_gpio(internal);

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = config->clock_speed_hz;
    device_config.mode = 0;
    device_config.spics_io_num = -1;
    device_config.queue_size = 1;
    if (spi_bus_add_device(spi_config->host, &device_config, &internal->spi) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device");
        internal->spi = nullptr;
        destroy(internal);
        return ERROR_RESOURCE;
    }

    spi_controller_lock(parent);
    hardware_init(internal);
    spi_controller_unlock(parent);
    if (internal->update_failed) {
        LOG_E(TAG, "Panel does not respond");
        destroy(internal);
        return ERROR_RESOURCE;
    }


    if (xTaskCreate(update_task, "ssd1677", 4096, internal, 4, &internal->task) != pdPASS) {
        LOG_E(TAG, "Failed to create update task");
        destroy(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    // The panel keeps the image from before the reset
    clear_blocking(internal);

    LOG_I(TAG, "Started %dx%d, rotation %u", PANEL_WIDTH, PANEL_HEIGHT, config->rotation);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Ssd1677Internal*>(device_get_driver_data(device));

    notify_task(internal, CMD_EXIT);
    while (!internal->exited) {
        vTaskDelay(1);
    }

    device_set_driver_data(device, nullptr);
    destroy(internal);
    return ERROR_NONE;
}

// endregion

Driver ssd1677_driver = {
    .name = "ssd1677",
    .compatible = (const char*[]) { "solomon-systech,ssd1677", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &ssd1677_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};
