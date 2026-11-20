// SPDX-License-Identifier: Apache-2.0
#include "gdep103tc2_engine.h"
#include "gdep103tc2_encode.h"

#include <tactility/log.h>

#include <freertos/FreeRTOS.h>

#define TAG "Gdep103tc2Engine"

static constexpr uint32_t IDLE_POWER_OFF_MS = 2000;
static constexpr uint32_t STOP_POLL_TICKS = 1;
static constexpr uint32_t TASK_STACK_BYTES = 6144;
static constexpr UBaseType_t TASK_PRIORITY = 1;
static constexpr BaseType_t TASK_CORE = 1;

static constexpr uint32_t CMD_UPDATE = 1U << 0;
static constexpr uint32_t CMD_FLASH = 1U << 1;
static constexpr uint32_t CMD_POWER_OFF = 1U << 2;
static constexpr uint32_t CMD_EXIT = 1U << 3;

static void notify(Gdep103tc2Engine* engine, uint32_t command) {
    xTaskNotify(engine->task, command, eSetBits);
}

static Gdep103tc2Region take_pending(Gdep103tc2Engine* engine) {
    portENTER_CRITICAL(&engine->lock);
    const Gdep103tc2Region pending = engine->pending;
    engine->pending = EMPTY_REGION;
    portEXIT_CRITICAL(&engine->lock);
    return pending;
}

static void log_bus_errors(Gdep103tc2Engine* engine) {
    const uint32_t errors = gdep103tc2_bus_take_errors(&engine->bus);
    if (errors > 0) {
        LOG_W(TAG, "%lu rows were not sent", static_cast<unsigned long>(errors));
    }
}

static void power_down(Gdep103tc2Engine* engine) {
    gdep103tc2_power_off(&engine->power);
    gdep103tc2_bus_power_down(&engine->bus);
}

static void handle_flash(Gdep103tc2Engine* engine) {
    if (gdep103tc2_power_on(&engine->power) == ERROR_NONE) {
        gdep103tc2_run_flash(&engine->bus, &engine->frames);
        log_bus_errors(engine);
        gdep103tc2_frames_settle_white(&engine->frames);
    }
    if (!engine->flash_wipes_content) {
        portENTER_CRITICAL(&engine->lock);
        engine->pending = FULL_REGION;
        portEXIT_CRITICAL(&engine->lock);
    }
    xSemaphoreGive(engine->request_done);
}

// Columns are processed in groups of 8 pixels for ease of use
static Gdep103tc2Region align_columns(const Gdep103tc2Region& region) {
    Gdep103tc2Region aligned = region;
    aligned.x0 &= ~7;
    aligned.x1 = (aligned.x1 + 7) & ~7;
    return aligned;
}

// Streams one frame for the active region. Returns false when the supply failed.
static bool drive_frame(Gdep103tc2Engine* engine, const Gdep103tc2Region& active) {
    if (gdep103tc2_power_on(&engine->power) != ERROR_NONE) return false;

    gdep103tc2_run_update_frame(&engine->bus, &engine->frames, align_columns(active));
    return true;
}

// Grayscale draws a region in one go
static void handle_gray(Gdep103tc2Engine* engine, const Gdep103tc2Region& pending, bool notified) {
    if (!pending.empty()) {
        if (gdep103tc2_power_on(&engine->power) != ERROR_NONE) return;
        gdep103tc2_run_gray_update(&engine->bus, &engine->frames, align_columns(pending));
        log_bus_errors(engine);
    } else if (!notified) {
        power_down(engine);
    }
}

static void display_task(void* arg) {
    auto* engine = static_cast<Gdep103tc2Engine*>(arg);
    Gdep103tc2Region active = EMPTY_REGION;
    int frames_left = 0;

    while (true) {
        uint32_t commands = 0;
        TickType_t timeout = portMAX_DELAY;
        if (frames_left > 0) {
            timeout = 0;
        } else if (engine->power.on) {
            timeout = pdMS_TO_TICKS(IDLE_POWER_OFF_MS);
        }
        const bool notified = xTaskNotifyWait(0, UINT32_MAX, &commands, timeout) == pdTRUE;

        if (commands & CMD_EXIT) break;

        if (commands & CMD_POWER_OFF) {
            active = EMPTY_REGION;
            frames_left = 0;
            power_down(engine);
        }

        if (commands & CMD_FLASH) {
            active = EMPTY_REGION;
            frames_left = 0;
            handle_flash(engine);
        }

        const Gdep103tc2Region pending = take_pending(engine);
        if (engine->frames.grayscale) {
            handle_gray(engine, pending, notified);
            continue;
        }
        if (!pending.empty()) {
            active.merge(pending);
            frames_left = DRIVE_FRAMES;
        }

        if (frames_left > 0) {
            if (!drive_frame(engine, active)) {
                active = EMPTY_REGION;
                frames_left = 0;
                continue;
            }
            if (--frames_left == 0) {
                active = EMPTY_REGION;
                gdep103tc2_run_neutral(&engine->bus, &engine->frames);
                log_bus_errors(engine);
            }
        } else if (!notified) {
            power_down(engine);
        }
    }

    power_down(engine);
    engine->exited = true;
    vTaskDelete(nullptr);
}

error_t gdep103tc2_engine_start(Gdep103tc2Engine* engine, const Gdep103tc2Config* config) {
    engine->request_mutex = xSemaphoreCreateMutex();
    engine->request_done = xSemaphoreCreateBinary();
    if (engine->request_mutex == nullptr || engine->request_done == nullptr) {
        gdep103tc2_engine_stop(engine);
        return ERROR_OUT_OF_MEMORY;
    }

    Gdep103tc2Pins pins;
    error_t error = gdep103tc2_pins_resolve(config, &pins);
    if (error == ERROR_NONE) error = gdep103tc2_frames_init(&engine->frames, config);
    if (error == ERROR_NONE) error = gdep103tc2_power_init(&engine->power, config);
    if (error == ERROR_NONE) error = gdep103tc2_bus_init(&engine->bus, pins, config->pixel_clock_hz);
    if (error != ERROR_NONE) {
        gdep103tc2_engine_stop(engine);
        return error;
    }

    if (xTaskCreatePinnedToCore(display_task, "gdep103tc2", TASK_STACK_BYTES, engine, TASK_PRIORITY, &engine->task, TASK_CORE) != pdPASS) {
        LOG_E(TAG, "Failed to create the display task");
        gdep103tc2_engine_stop(engine);
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

void gdep103tc2_engine_stop(Gdep103tc2Engine* engine) {
    if (engine->task != nullptr) {
        notify(engine, CMD_EXIT);
        while (!engine->exited) {
            vTaskDelay(STOP_POLL_TICKS);
        }
        engine->task = nullptr;
    }
    gdep103tc2_bus_deinit(&engine->bus);
    gdep103tc2_power_deinit(&engine->power);
    gdep103tc2_frames_deinit(&engine->frames);
    if (engine->request_mutex != nullptr) vSemaphoreDelete(engine->request_mutex);
    if (engine->request_done != nullptr) vSemaphoreDelete(engine->request_done);
    engine->request_mutex = nullptr;
    engine->request_done = nullptr;
}

void gdep103tc2_engine_submit(Gdep103tc2Engine* engine, const Gdep103tc2Region& changed) {
    portENTER_CRITICAL(&engine->lock);
    engine->pending.merge(changed);
    portEXIT_CRITICAL(&engine->lock);
    notify(engine, CMD_UPDATE);
}

error_t gdep103tc2_engine_flash(Gdep103tc2Engine* engine, bool wipe_content) {
    xSemaphoreTake(engine->request_mutex, portMAX_DELAY);
    engine->flash_wipes_content = wipe_content;
    notify(engine, CMD_FLASH);
    xSemaphoreTake(engine->request_done, portMAX_DELAY);
    xSemaphoreGive(engine->request_mutex);
    return ERROR_NONE;
}

void gdep103tc2_engine_power_off(Gdep103tc2Engine* engine) {
    notify(engine, CMD_POWER_OFF);
}
