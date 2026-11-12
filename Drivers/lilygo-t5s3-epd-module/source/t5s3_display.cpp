// SPDX-License-Identifier: Apache-2.0
#include <drivers/t5s3_display.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/module.h>

#include "t5s3_epd_board.h"

#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#define TAG "T5s3Display"
#define GET_CONFIG(device) (static_cast<const T5s3DisplayConfig*>((device)->config))

// Gray levels are drawn without waveform tables, with a dose of frames per pixel in the manner of InkBoy
// (https://github.com/kuslota/InkBoy). A pixel whose target shade changes gets a program: black is driven dark for
// BLACK_FRAMES frames. White and the grays are driven light for LIGHT_FRAMES frames, which erases what the pixel showed
// before. The grays then rest for a few frames and are driven dark for the dose of their shade. A gray then looks the
// same whatever the pixel came from and moving content leaves no trails. Pixels whose target does not change are never
// driven.
//
// Every frame covers the rows from the top down to the lowest row of the current updates, so all of them change at once.
// The helper task encodes the next frame while the display task sends the current one.

static constexpr int PANEL_WIDTH = T5S3_EPD_WIDTH;
static constexpr int PANEL_HEIGHT = T5S3_EPD_HEIGHT;

// Shades are 4 bits per pixel with the even pixel of a pair in the low nibble. Programs are 1 byte per pixel.
static constexpr size_t SHADE_ROW_BYTES = PANEL_WIDTH / 2;
static constexpr size_t SHADE_SIZE = SHADE_ROW_BYTES * PANEL_HEIGHT;
static constexpr size_t PROGRAM_SIZE = static_cast<size_t>(PANEL_WIDTH) * PANEL_HEIGHT;
static constexpr size_t BUS_SIZE = static_cast<size_t>(T5S3_EPD_BUS_ROW_BYTES) * PANEL_HEIGHT;

// Rows are split in chunks of 32 pixels, a bit per chunk tells which ones have work
static constexpr int CHUNK_PIXELS = 32;
static constexpr int CHUNKS = PANEL_WIDTH / CHUNK_PIXELS;
static_assert(CHUNKS <= 32);

// Shades from black to white. The dose of a gray is its number of dark frames after the light frames and the frames of
// rest before them. Each frame of rest makes the dose a little darker, which gives shades between those of whole frames.
// The brightness of each shade was measured with a camera, 0 is black and 255 is white.
static constexpr int SHADES = 16;
static constexpr uint8_t SHADE_BLACK = 0;
static constexpr uint8_t SHADE_WHITE = SHADES - 1;
static constexpr uint8_t SHADE_DOSE[SHADES] = { 0, 8, 6, 5, 5, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 0 };
static constexpr uint8_t SHADE_REST[SHADES] = { 0, 0, 1, 2, 0, 1, 0, 2, 1, 0, 3, 1, 0, 3, 0, 0 };
static constexpr uint8_t SHADE_BRIGHTNESS[SHADES] = { 0, 21, 35, 47, 59, 70, 82, 94, 106, 120, 135, 155, 173, 196, 223, 255 };

// Between 10 and 30 light frames the grays come out the same whatever the pixel showed before, fewer leave traces
static constexpr uint8_t LIGHT_FRAMES = 15;
// Black is driven until grays that started with it ended their light frames. Pixels driven light wash out a thin black
// line next to them, for example a line of 1 pixel on a gray area that is drawn again after a refresh.
static constexpr uint8_t BLACK_FRAMES = LIGHT_FRAMES + 3;

// A program byte is 0 for a pixel at rest, the dark frames left below HOLD_BASE, HOLD_BASE plus the frames of rest left
// below LIGHT_BASE and LIGHT_BASE plus the light frames left above. Rest and the dark frames of the goal follow the light frames.
static constexpr uint8_t HOLD_BASE = 32;
static constexpr uint8_t LIGHT_BASE = 64;

// Shade nibbles of white pixels
static constexpr uint8_t WHITE_PAIR = SHADE_WHITE | (SHADE_WHITE << 4);

// The dose of a frame depends on its period, so every frame has this period, measured from start to start. A frame of
// all rows takes about 34 ms with a pixel clock of 20 MHz, the doses were measured with that clock.
static constexpr int64_t FRAME_PERIOD_US = 40000;

// Gate pulse of a row in units of 0.1 us
static constexpr uint32_t ROW_HOLD_TICKS = 120;

// Groups of 8 pixels that get new programs in a frame. A larger change takes a few frames to start, so its encoding
// does not delay the next frame. The first frame of an update has no limit.
static constexpr int RETARGET_WORDS = 6000;

// A frame that comes this much late or takes longer than the period gave some pixels a clearly wrong dose
static constexpr int64_t STALL_US = 10000;
// A frame can also come late because its encoding took longer than the period, for example when many pixels end their
// light frames at once. That only lengthens one frame, so it counts as a stall from this lateness on.
static constexpr int64_t ENCODING_STALL_US = 150000;

// Frames whose chunks a restart of the doses covers. A stall extends the drive of the frame before it and of the stalled
// frame, and the encoding runs one frame ahead.
static constexpr int RECENT_FRAMES = 3;

// Rows are timed by busy waiting, so the display task runs above the LVGL task. The helper task encodes on the other core.
static constexpr UBaseType_t TASK_PRIORITY = 7;
static constexpr BaseType_t TASK_CORE = 0;
static constexpr BaseType_t HELPER_CORE = 1;

// Frames of the flash that clear() runs on the whole panel
static constexpr int FLASH_DARK_FRAMES = 12;
static constexpr int FLASH_LIGHT_FRAMES = 16;

static constexpr uint32_t IDLE_POWER_OFF_MS = 2000;

// Pixels that rested for minutes keep faint traces of what they showed, which only a flash removes. Tactility calls
// refresh() after the user stopped touching the screen. Content that changes without touches gets a refresh from the
// driver once it was quiet for AUTO_REFRESH_QUIET_MS, at most every MINIMAL_AUTO_REFRESH_INTERVAL_MS.
static constexpr int64_t MINIMAL_AUTO_REFRESH_INTERVAL_MS = 10 * 60 * 1000;
static constexpr int64_t AUTO_REFRESH_QUIET_MS = 5000;

static constexpr uint32_t CMD_UPDATE = 1U << 0;
static constexpr uint32_t CMD_FLASH = 1U << 1;
static constexpr uint32_t CMD_POWER_OFF = 1U << 2;
static constexpr uint32_t CMD_EXIT = 1U << 3;
static constexpr uint32_t CMD_REFRESH = 1U << 4;

// 4x4 ordered (Bayer) dither thresholds
static constexpr uint8_t BAYER_4X4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

// A luminance lies between two shades and is dithered between them
struct Tone {
    uint8_t darker;
    uint8_t lighter;
    // Share of the lighter shade, 0 to 16
    uint8_t share;
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

extern "C" {

// Result of encoding a frame
struct Encoded {
    bool driven;
    // Rows [0, rows) hold all driven pixels
    int rows;
};

// Work of one encoding call: rows [y0, y1) into bus, with a budget of RETARGET_WORDS or none when negative
struct EncodeJob {
    uint8_t* bus;
    int y0, y1;
    int budget;
    Encoded result;
    int64_t duration_us;
};

struct T5s3DisplayInternal {
    TaskHandle_t task;
    SemaphoreHandle_t request_mutex;
    SemaphoreHandle_t request_done;
    portMUX_TYPE lock;
    volatile bool exited;
    bool powered;
    // Time of the oldest change not yet picked up and the longest draw_bitmap() call since, guarded by lock
    int64_t pending_since_us;
    int64_t draw_max_us;
    // Chunks of each row with new targets, set by draw_bitmap() and taken by the encoder
    std::atomic<uint32_t>* dirty;
    // Chunks of each row with running programs, only used by the encoder
    uint32_t* running;
    // Chunks of each row that ran in each of the last frames, only used by the encoder
    uint32_t* recent[RECENT_FRAMES];
    int recent_index;
    // Shade that draw_bitmap() wants for each pixel
    uint8_t* target;
    // Shade that the program of each pixel draws or has drawn
    uint8_t* goal;
    // Program of each pixel, see LIGHT_BASE
    uint8_t* program;
    // Bus rows of two frames, the one being sent and the next one
    uint8_t* bus[2];
    // Bus rows of the frame being sent
    const uint8_t* sending;
    // Encoding on the other core
    TaskHandle_t helper;
    SemaphoreHandle_t helper_start;
    SemaphoreHandle_t helper_done;
    volatile bool helper_exit;
    volatile bool helper_exited;
    EncodeJob helper_job;
    // Set by the display task after a late or stalled frame, the helper task then restarts the doses
    bool restart;
    // Shades for each luminance
    Tone* tones;
};

static inline uint8_t get_nibble(const uint8_t* buffer, int x) {
    return (buffer[x / 2] >> ((x & 1) * 4)) & 0x0F;
}

static inline void set_nibble(uint8_t* buffer, int x, uint8_t value) {
    uint8_t* byte = &buffer[x / 2];
    const int shift = (x & 1) * 4;
    *byte = static_cast<uint8_t>((*byte & ~(0x0F << shift)) | (value << shift));
}

// region Pixel programs

// Gives the pixels of a chunk whose target changed a new program. A pixel that is still in its light frames keeps them,
// so its gray is reached the same way. Returns the groups of 8 pixels that had changes.
static int retarget_chunk(T5s3DisplayInternal* internal, int y, int chunk) {
    const int x0 = chunk * CHUNK_PIXELS;
    uint8_t* program = internal->program + y * PANEL_WIDTH + x0;
    uint8_t* goal = internal->goal + y * SHADE_ROW_BYTES + x0 / 2;
    const auto* target_words = reinterpret_cast<const uint32_t*>(internal->target + y * SHADE_ROW_BYTES + x0 / 2);
    auto* goal_words = reinterpret_cast<uint32_t*>(goal);
    int changed = 0;
    for (int word = 0; word < CHUNK_PIXELS / 8; word++) {
        // Most chunks that LVGL draws again keep their shades
        const uint32_t wanted = target_words[word];
        const uint32_t current = goal_words[word];
        if (wanted == current) continue;
        changed++;
        for (int i = 0; i < 8; i++) {
            const uint8_t shade = (wanted >> (i * 4)) & 0x0F;
            if (shade == ((current >> (i * 4)) & 0x0F)) continue;
            uint8_t& frames = program[word * 8 + i];
            if (shade == SHADE_BLACK) {
                frames = BLACK_FRAMES;
            } else if (frames <= LIGHT_BASE) {
                frames = LIGHT_BASE + LIGHT_FRAMES;
            }
        }
        goal_words[word] = wanted;
    }
    return changed;
}

// Advances the programs of the pixels of a chunk by one frame and writes their actions, 2 bits per pixel with the first
// pixel in the lowest bits, 01 drives dark and 10 drives light. Sets driven when a pixel is driven and returns whether
// a program still runs. Four programs are stepped at once in a 32 bit word, all program bytes are below 128.
static bool step_chunk(T5s3DisplayInternal* internal, int y, int chunk, uint8_t* out, bool* driven) {
    const int x0 = chunk * CHUNK_PIXELS;
    auto* words = reinterpret_cast<uint32_t*>(internal->program + y * PANEL_WIDTH + x0);
    const uint8_t* goal = internal->goal + y * SHADE_ROW_BYTES + x0 / 2;
    uint32_t running = 0;
    for (int word = 0; word < CHUNK_PIXELS / 4; word++) {
        uint32_t value = words[word];
        if (value == 0) {
            out[word] = 0;
            continue;
        }
        // Top bit of each byte: the program runs. Bit 6: the pixel is driven light. Bit 5 without bit 6: it rests.
        const uint32_t running_bits = ((value & 0x7F7F7F7F) + 0x7F7F7F7F) & 0x80808080;
        const uint32_t light_bits = value & 0x40404040;
        const uint32_t hold_bits = value & 0x20202020 & ~(light_bits >> 1);
        value -= running_bits >> 7;
        // Pixels whose light frames end rest, pixels whose rest ends get the dark frames of their goal
        const uint32_t light_ended = value ^ 0x40404040;
        const uint32_t hold_ended = value ^ 0x20202020;
        if ((light_bits != 0 && ((light_ended - 0x01010101) & ~light_ended & 0x80808080) != 0) ||
            (hold_bits != 0 && ((hold_ended - 0x01010101) & ~hold_ended & 0x80808080) != 0)) {
            for (int i = 0; i < 4; i++) {
                const uint32_t frames = (value >> (i * 8)) & 0xFF;
                const int x = word * 4 + i;
                uint32_t next = frames;
                if (frames == LIGHT_BASE) {
                    const uint8_t shade = get_nibble(goal, x);
                    next = SHADE_REST[shade] != 0 ? HOLD_BASE + SHADE_REST[shade] : SHADE_DOSE[shade];
                } else if (frames == HOLD_BASE) {
                    next = SHADE_DOSE[get_nibble(goal, x)];
                }
                value = (value & ~(0xFFU << (i * 8))) | (next << (i * 8));
            }
        }
        words[word] = value;
        running |= value;
        // 2 bits per pixel at the bottom of each byte, then packed into one byte
        const uint32_t light = light_bits >> 6;
        const uint32_t dark = (running_bits >> 7) & ~light & ~(hold_bits >> 5);
        const uint32_t actions = (light << 1) | dark;
        *driven |= (light | dark) != 0;
        out[word] = static_cast<uint8_t>((actions & 0x03) | ((actions >> 6) & 0x0C) | ((actions >> 12) & 0x30) | ((actions >> 18) & 0xC0));
    }
    return running != 0;
}

// Encodes the rows of a job for the next frame. Chunks with new targets beyond the budget keep them for a later frame.
static void encode(T5s3DisplayInternal* internal, EncodeJob* job) {
    const int64_t start_us = esp_timer_get_time();
    uint32_t* recent = internal->recent[internal->recent_index];
    int budget = job->budget;
    job->result = { false, 0 };
    for (int y = job->y0; y < job->y1; y++) {
        uint8_t* out = job->bus + y * T5S3_EPD_BUS_ROW_BYTES;
        const uint32_t dirty = budget != 0 ? internal->dirty[y].exchange(0) : 0;
        uint32_t deferred = 0;
        uint32_t work = dirty;
        while (work != 0) {
            const int chunk = __builtin_ctz(work);
            work &= work - 1;
            if (budget == 0) {
                deferred |= 1U << chunk;
                continue;
            }
            const int changed = retarget_chunk(internal, y, chunk);
            if (budget > 0) {
                budget = std::max(0, budget - changed);
            }
            internal->running[y] |= 1U << chunk;
        }
        if (deferred != 0) {
            internal->dirty[y].fetch_or(deferred);
        }
        work = internal->running[y];
        recent[y] = work;
        if (work == 0) {
            memset(out, 0, T5S3_EPD_BUS_ROW_BYTES);
            continue;
        }
        uint32_t running = 0;
        bool driven = false;
        for (int chunk = 0; chunk < CHUNKS; chunk++) {
            uint8_t* chunk_out = out + chunk * CHUNK_PIXELS / 4;
            if ((work & (1U << chunk)) == 0) {
                memset(chunk_out, 0, CHUNK_PIXELS / 4);
            } else if (step_chunk(internal, y, chunk, chunk_out, &driven)) {
                running |= 1U << chunk;
            }
        }
        internal->running[y] = running;
        if (driven) {
            job->result.driven = true;
            job->result.rows = y + 1;
        }
    }
    job->duration_us = esp_timer_get_time() - start_us;
}

// The encoders of a frame write to the same slot of recent chunks, the next frame uses the next slot
static void next_recent(T5s3DisplayInternal* internal) {
    internal->recent_index = (internal->recent_index + 1) % RECENT_FRAMES;
}

// Starts the programs of the gray pixels of the chunks that ran in the last frames again. A frame that came late or
// stalled, for example while the flash was written, gave the ones in their rest or dark frames a wrong dose, also the
// ones whose program ended since.
static void restart_doses(T5s3DisplayInternal* internal) {
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        uint32_t work = 0;
        for (const uint32_t* recent : internal->recent) {
            work |= recent[y];
        }
        internal->running[y] |= work;
        while (work != 0) {
            const int chunk = __builtin_ctz(work);
            work &= work - 1;
            const int x0 = chunk * CHUNK_PIXELS;
            uint8_t* program = internal->program + y * PANEL_WIDTH + x0;
            const uint8_t* goal = internal->goal + y * SHADE_ROW_BYTES + x0 / 2;
            for (int x = 0; x < CHUNK_PIXELS; x++) {
                const uint8_t shade = get_nibble(goal, x);
                if (program[x] > LIGHT_BASE || shade == SHADE_BLACK || shade == SHADE_WHITE) continue;
                program[x] = LIGHT_BASE + LIGHT_FRAMES;
            }
        }
    }
}

static void helper_task(void* arg) {
    auto* internal = static_cast<T5s3DisplayInternal*>(arg);
    while (true) {
        xSemaphoreTake(internal->helper_start, portMAX_DELAY);
        if (internal->helper_exit) break;
        if (internal->restart) {
            internal->restart = false;
            restart_doses(internal);
        }
        encode(internal, &internal->helper_job);
        xSemaphoreGive(internal->helper_done);
    }
    internal->helper_exited = true;
    vTaskDelete(nullptr);
}

static void start_helper(T5s3DisplayInternal* internal, uint8_t* bus, int y0, int y1, int budget) {
    next_recent(internal);
    internal->helper_job = { bus, y0, y1, budget, { false, 0 }, 0 };
    xSemaphoreGive(internal->helper_start);
}

static Encoded finish_helper(T5s3DisplayInternal* internal) {
    xSemaphoreTake(internal->helper_done, portMAX_DELAY);
    return internal->helper_job.result;
}

// Encodes a whole frame on both cores without a budget
static Encoded encode_now(T5s3DisplayInternal* internal, uint8_t* bus) {
    // restart_doses() covers all rows, so it must not run on the helper while this task encodes the upper half
    if (internal->restart) {
        internal->restart = false;
        restart_doses(internal);
    }
    start_helper(internal, bus, PANEL_HEIGHT / 2, PANEL_HEIGHT, -1);
    EncodeJob job = { bus, 0, PANEL_HEIGHT / 2, -1, { false, 0 }, 0 };
    encode(internal, &job);
    const Encoded lower = finish_helper(internal);
    return { job.result.driven || lower.driven, lower.driven ? lower.rows : job.result.rows };
}

static void IRAM_ATTR copy_bus_row(int y, uint8_t* out, void* context) {
    auto* internal = static_cast<T5s3DisplayInternal*>(context);
    memcpy(out, internal->sending + y * T5S3_EPD_BUS_ROW_BYTES, T5S3_EPD_BUS_ROW_BYTES);
}

// endregion

// region Task

static void power_on(T5s3DisplayInternal* internal) {
    if (internal->powered) return;
    t5s3_epd_board_power_on();
    internal->powered = true;
}

static void power_off(T5s3DisplayInternal* internal) {
    if (!internal->powered) return;
    t5s3_epd_board_power_off();
    internal->powered = false;
}

// Drives every pixel toward one color
static bool run_solid_frames(uint8_t pattern, int frames) {
    t5s3_epd_board_fill_rows(pattern);
    bool success = true;
    for (int frame = 0; frame < frames && success; frame++) {
        success = t5s3_epd_board_frame(0, PANEL_HEIGHT, nullptr, ROW_HOLD_TICKS, nullptr, nullptr);
    }
    return success;
}

// Flashes the panel to white, all pixels show white afterwards
static void run_flash(T5s3DisplayInternal* internal) {
    const int64_t start_us = esp_timer_get_time();
    power_on(internal);
    const bool success = run_solid_frames(T5S3_EPD_ALL_DARK, FLASH_DARK_FRAMES) &&
        run_solid_frames(T5S3_EPD_ALL_LIGHT, FLASH_LIGHT_FRAMES) &&
        run_solid_frames(0, 1);
    if (!success) {
        LOG_E(TAG, "Flash failed, a peripheral did not respond");
    }
    memset(internal->goal, WHITE_PAIR, SHADE_SIZE);
    memset(internal->program, 0, PROGRAM_SIZE);
    memset(internal->running, 0, PANEL_HEIGHT * sizeof(uint32_t));
    for (uint32_t* recent : internal->recent) {
        memset(recent, 0, PANEL_HEIGHT * sizeof(uint32_t));
    }
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        internal->dirty[y].store(0);
    }
    LOG_I(TAG, "Flashed in %d ms", static_cast<int>((esp_timer_get_time() - start_us) / 1000));
}

// Flashes the panel and draws all pixels that are not white again
static void run_refresh(T5s3DisplayInternal* internal) {
    run_flash(internal);
    constexpr uint32_t ALL_CHUNKS = CHUNKS == 32 ? ~0U : (1U << CHUNKS) - 1;
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        internal->dirty[y].store(ALL_CHUNKS);
    }
}

// Waits for the start of the next frame, mostly asleep so other tasks run and the idle task feeds the watchdog
static void wait_until(int64_t time_us) {
    const int64_t remaining_us = time_us - esp_timer_get_time();
    if (remaining_us > 2000) {
        vTaskDelay(pdMS_TO_TICKS((remaining_us - 1000) / 1000));
    }
    while (esp_timer_get_time() < time_us) {}
}

static void display_task(void* arg) {
    auto* internal = static_cast<T5s3DisplayInternal*>(arg);
    bool active = false;
    int current = 0;
    Encoded frame = { false, 0 };
    // Rows of the frames of the current update, they only grow so that every row keeps its timing
    int rows = 0;
    int64_t next_frame_us = 0;
    // Statistics of the current update
    int frames = 0;
    int64_t since_us = 0;
    int64_t longest_frame_us = 0;
    int64_t longest_encode_us = 0;
    // The encoding of the next frame ended after its start time
    bool encoding_late = false;
    // Updates since the last refresh, except the one that draws the refreshed content
    bool changed_since_refresh = false;
    bool refresh_update = false;
    int64_t last_refresh_us = esp_timer_get_time();
    int64_t last_change_us = 0;

    while (true) {
        uint32_t commands = 0;
        TickType_t timeout = portMAX_DELAY;
        const int64_t refresh_due_us = changed_since_refresh ?
            std::max(last_refresh_us + MINIMAL_AUTO_REFRESH_INTERVAL_MS * 1000, last_change_us + AUTO_REFRESH_QUIET_MS * 1000) : INT64_MAX;
        if (active) {
            timeout = 0;
        } else {
            int64_t wait_us = internal->powered ? IDLE_POWER_OFF_MS * 1000LL : INT64_MAX;
            if (refresh_due_us != INT64_MAX) {
                wait_us = std::min(wait_us, std::max<int64_t>(0, refresh_due_us - esp_timer_get_time()));
            }
            if (wait_us != INT64_MAX) {
                timeout = pdMS_TO_TICKS(wait_us / 1000 + 1);
            }
        }
        const bool notified = xTaskNotifyWait(0, UINT32_MAX, &commands, timeout) == pdTRUE;

        if (commands & CMD_EXIT) {
            break;
        }

        if (commands & CMD_POWER_OFF) {
            active = false;
            power_off(internal);
        }

        if (commands & CMD_FLASH) {
            run_flash(internal);
            active = false;
            changed_since_refresh = false;
            last_refresh_us = esp_timer_get_time();
            xSemaphoreGive(internal->request_done);
        }

        if ((commands & CMD_REFRESH) || (!active && esp_timer_get_time() >= refresh_due_us)) {
            run_refresh(internal);
            active = false;
            changed_since_refresh = false;
            refresh_update = true;
            last_refresh_us = esp_timer_get_time();
        }

        if (!active) {
            current = 0;
            frame = encode_now(internal, internal->bus[current]);
            if (!frame.driven) {
                if (!notified) {
                    power_off(internal);
                }
                continue;
            }
            active = true;
            changed_since_refresh |= !refresh_update;
            refresh_update = false;
            rows = 0;
            frames = 0;
            longest_frame_us = 0;
            longest_encode_us = 0;
            portENTER_CRITICAL(&internal->lock);
            since_us = internal->pending_since_us;
            const int64_t draw_max_us = internal->draw_max_us;
            internal->pending_since_us = 0;
            internal->draw_max_us = 0;
            portEXIT_CRITICAL(&internal->lock);
            LOG_D(TAG, "Update starts %d ms after the first change (draw_bitmap up to %d us)",
                since_us != 0 ? static_cast<int>((esp_timer_get_time() - since_us) / 1000) : -1, static_cast<int>(draw_max_us));
            power_on(internal);
            next_frame_us = esp_timer_get_time();
        }

        // The next frame is encoded while this one is sent. A frame that drives nothing ends the drive of the one before.
        start_helper(internal, internal->bus[current ^ 1], 0, PANEL_HEIGHT, RETARGET_WORDS);
        rows = std::max(rows, frame.rows);
        internal->sending = internal->bus[current];
        wait_until(next_frame_us);
        const int64_t frame_start_us = esp_timer_get_time();
        const int64_t late_us = frame_start_us - next_frame_us;
        if (!t5s3_epd_board_frame(0, rows, nullptr, ROW_HOLD_TICKS, copy_bus_row, internal)) {
            LOG_E(TAG, "Frame failed, a peripheral did not respond");
        }
        const int64_t frame_us = esp_timer_get_time() - frame_start_us;
        longest_frame_us = std::max(longest_frame_us, frame_us);
        next_frame_us = frame_start_us + FRAME_PERIOD_US;
        frames++;
        const Encoded next = finish_helper(internal);
        // A late or stalled frame drives some pixels for longer than their dose. The flag is set while the helper is
        // idle, it restarts the doses with its next job.
        if (late_us > (encoding_late ? ENCODING_STALL_US : STALL_US) || frame_us > FRAME_PERIOD_US + STALL_US) {
            internal->restart = true;
            LOG_W(TAG, "Frame %d us late and %d us long, doses start again", static_cast<int>(late_us), static_cast<int>(frame_us));
        }
        encoding_late = esp_timer_get_time() > next_frame_us;
        longest_encode_us = std::max(longest_encode_us, internal->helper_job.duration_us);

        if (!frame.driven && !next.driven) {
            LOG_D(TAG, "Drew rows 0 to %d in %d frames, longest frame %d us, longest encoding %d us", rows, frames,
                static_cast<int>(longest_frame_us), static_cast<int>(longest_encode_us));
            active = false;
            last_change_us = esp_timer_get_time();
            continue;
        }
        current ^= 1;
        frame = next;
    }

    power_off(internal);
    internal->exited = true;
    vTaskDelete(nullptr);
}

// endregion

// region Helpers

static void notify_task(T5s3DisplayInternal* internal, uint32_t command) {
    xTaskNotify(internal->task, command, eSetBits);
}

static error_t flash_blocking(T5s3DisplayInternal* internal) {
    xSemaphoreTake(internal->request_mutex, portMAX_DELAY);
    notify_task(internal, CMD_FLASH);
    xSemaphoreTake(internal->request_done, portMAX_DELAY);
    xSemaphoreGive(internal->request_mutex);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t t5s3_display_reset(Device* device) {
    auto* internal = static_cast<T5s3DisplayInternal*>(device_get_driver_data(device));
    // The panel has no reset line, it powers up again with the next update
    notify_task(internal, CMD_POWER_OFF);
    return ERROR_NONE;
}

/** Initialization is done in start() */
static error_t t5s3_display_init(Device*) {
    return ERROR_NONE;
}

/** Flashes the panel to white */
static error_t t5s3_display_clear(Device* device) {
    auto* internal = static_cast<T5s3DisplayInternal*>(device_get_driver_data(device));
    memset(internal->target, WHITE_PAIR, SHADE_SIZE);
    return flash_blocking(internal);
}

/** Flashes the panel and draws the content again, which removes all ghosting. Does not wait for the panel. */
static error_t t5s3_display_refresh(Device* device) {
    auto* internal = static_cast<T5s3DisplayInternal*>(device_get_driver_data(device));
    notify_task(internal, CMD_REFRESH);
    return ERROR_NONE;
}

// Reports GRAYSCALE8 so that draw_bitmap() is called per changed tile. The luminance is dithered between the two nearest shades.
static error_t t5s3_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<T5s3DisplayInternal*>(device_get_driver_data(device));
    const int64_t start_us = esp_timer_get_time();
    const auto* source = static_cast<const uint8_t*>(color_data);
    const int32_t width = x_end - x_start;
    const int first_chunk = x_start / CHUNK_PIXELS;
    const int last_chunk = (x_end - 1) / CHUNK_PIXELS;
    const uint32_t chunks = (last_chunk == 31 ? ~0U : (1U << (last_chunk + 1)) - 1) & ~((1U << first_chunk) - 1);
    for (int32_t y = y_start; y < y_end; y++) {
        const uint8_t* source_row = source + (y - y_start) * width;
        const uint8_t* thresholds = BAYER_4X4[y & 3];
        uint8_t* target_row = internal->target + y * SHADE_ROW_BYTES;
        for (int32_t x = x_start; x < x_end; x++) {
            const Tone& tone = internal->tones[source_row[x - x_start]];
            set_nibble(target_row, x, tone.share > thresholds[x & 3] ? tone.lighter : tone.darker);
        }
        // The targets are written before their chunks are marked
        internal->dirty[y].fetch_or(chunks);
    }

    const int64_t draw_us = esp_timer_get_time() - start_us;
    portENTER_CRITICAL(&internal->lock);
    if (internal->pending_since_us == 0) {
        internal->pending_since_us = start_us;
    }
    internal->draw_max_us = std::max(internal->draw_max_us, draw_us);
    portEXIT_CRITICAL(&internal->lock);
    notify_task(internal, CMD_UPDATE);
    return ERROR_NONE;
}

static error_t t5s3_display_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<T5s3DisplayInternal*>(device_get_driver_data(device));
    if (!on_off) {
        notify_task(internal, CMD_POWER_OFF);
    }
    return ERROR_NONE;
}

static DisplayColorFormat t5s3_display_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

static uint16_t t5s3_display_get_resolution_x(Device*) {
    return PANEL_WIDTH;
}

static uint16_t t5s3_display_get_resolution_y(Device*) {
    return PANEL_HEIGHT;
}

// endregion

static const DisplayApi t5s3_display_api = {
    // The driver converts into its own buffer, so the LVGL draw buffers may live in external RAM
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
    .reset = t5s3_display_reset,
    .init = t5s3_display_init,
    .draw_bitmap = t5s3_display_draw_bitmap,
    .clear = t5s3_display_clear,
    .refresh = t5s3_display_refresh,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = t5s3_display_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = t5s3_display_get_color_format,
    .get_resolution_x = t5s3_display_get_resolution_x,
    .get_resolution_y = t5s3_display_get_resolution_y,
    // The framebuffer is 4bpp and not in the reported GRAYSCALE8 format
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

// region Driver lifecycle

static void stop_helper(T5s3DisplayInternal* internal) {
    internal->helper_exit = true;
    xSemaphoreGive(internal->helper_start);
    while (!internal->helper_exited) {
        vTaskDelay(1);
    }
}

static void destroy(T5s3DisplayInternal* internal) {
    if (internal->request_mutex) vSemaphoreDelete(internal->request_mutex);
    if (internal->request_done) vSemaphoreDelete(internal->request_done);
    if (internal->helper_start) vSemaphoreDelete(internal->helper_start);
    if (internal->helper_done) vSemaphoreDelete(internal->helper_done);
    memory_free(internal->dirty);
    memory_free(internal->running);
    for (uint32_t* recent : internal->recent) {
        memory_free(recent);
    }
    memory_free(internal->target);
    memory_free(internal->goal);
    memory_free(internal->program);
    memory_free(internal->bus[0]);
    memory_free(internal->bus[1]);
    memory_free(internal->tones);
    memory_free(internal);
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    static const MemoryPolicy external_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 16 };
    auto* internal = static_cast<T5s3DisplayInternal*>(memory_calloc_with_policy(1, sizeof(T5s3DisplayInternal), &internal_policy));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->lock = portMUX_INITIALIZER_UNLOCKED;
    internal->request_mutex = xSemaphoreCreateMutex();
    internal->request_done = xSemaphoreCreateBinary();
    internal->helper_start = xSemaphoreCreateBinary();
    internal->helper_done = xSemaphoreCreateBinary();
    // The chunk masks are read for every row of every frame, so they stay in internal RAM
    internal->dirty = static_cast<std::atomic<uint32_t>*>(memory_calloc_with_policy(PANEL_HEIGHT, sizeof(std::atomic<uint32_t>), &internal_policy));
    internal->running = static_cast<uint32_t*>(memory_calloc_with_policy(PANEL_HEIGHT, sizeof(uint32_t), &internal_policy));
    bool recent_allocated = true;
    for (uint32_t*& recent : internal->recent) {
        recent = static_cast<uint32_t*>(memory_calloc_with_policy(PANEL_HEIGHT, sizeof(uint32_t), &internal_policy));
        recent_allocated &= recent != nullptr;
    }
    internal->target = static_cast<uint8_t*>(memory_alloc_with_policy(SHADE_SIZE, &external_policy));
    internal->goal = static_cast<uint8_t*>(memory_alloc_with_policy(SHADE_SIZE, &external_policy));
    internal->program = static_cast<uint8_t*>(memory_calloc_with_policy(1, PROGRAM_SIZE, &external_policy));
    internal->bus[0] = static_cast<uint8_t*>(memory_alloc_with_policy(BUS_SIZE, &external_policy));
    internal->bus[1] = static_cast<uint8_t*>(memory_alloc_with_policy(BUS_SIZE, &external_policy));
    internal->tones = static_cast<Tone*>(memory_alloc_with_policy(256 * sizeof(Tone), &external_policy));
    if (!recent_allocated || !internal->request_mutex || !internal->request_done || !internal->helper_start || !internal->helper_done || !internal->dirty ||
        !internal->running || !internal->target || !internal->goal || !internal->program || !internal->bus[0] || !internal->bus[1] ||
        !internal->tones) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    memset(internal->target, WHITE_PAIR, SHADE_SIZE);
    memset(internal->goal, WHITE_PAIR, SHADE_SIZE);
    build_tones(internal->tones);

    if (!t5s3_epd_board_init(config->pixel_clock_hz)) {
        LOG_E(TAG, "Failed to set up the pins and peripherals");
        destroy(internal);
        return ERROR_RESOURCE;
    }

    if (xTaskCreatePinnedToCore(helper_task, "t5s3_display_helper", 3072, internal, TASK_PRIORITY, &internal->helper, HELPER_CORE) != pdPASS) {
        LOG_E(TAG, "Failed to create display helper task");
        t5s3_epd_board_deinit();
        destroy(internal);
        return ERROR_RESOURCE;
    }

    if (xTaskCreatePinnedToCore(display_task, "t5s3_display", 4096, internal, TASK_PRIORITY, &internal->task, TASK_CORE) != pdPASS) {
        LOG_E(TAG, "Failed to create display task");
        stop_helper(internal);
        t5s3_epd_board_deinit();
        destroy(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    // The boot splash leaves residue that a regular update would never clear
    flash_blocking(internal);

    LOG_I(TAG, "Started (%dx%d)", PANEL_WIDTH, PANEL_HEIGHT);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<T5s3DisplayInternal*>(device_get_driver_data(device));
    notify_task(internal, CMD_EXIT);
    while (!internal->exited) {
        vTaskDelay(1);
    }
    stop_helper(internal);
    device_set_driver_data(device, nullptr);
    t5s3_epd_board_deinit();
    destroy(internal);
    return ERROR_NONE;
}

// endregion

Driver t5s3_display_driver = {
    .name = "t5s3-display",
    .compatible = (const char*[]) { "lilygo,t5s3-display", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &t5s3_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

}
