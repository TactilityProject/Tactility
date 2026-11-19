// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_bus.h"
#include "gdep103tc2_frames.h"
#include "gdep103tc2_power.h"
#include "gdep103tc2_region.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

struct Gdep103tc2Engine {
    Gdep103tc2Power power;
    Gdep103tc2Bus bus;
    Gdep103tc2Frames frames;
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t request_mutex = nullptr;
    SemaphoreHandle_t request_done = nullptr;
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    volatile bool exited = false;
    bool flash_wipes_content = false;
    Gdep103tc2Region pending = EMPTY_REGION;
};

/** Allocates everything, powers nothing and starts the task. We should(n't) be like this /silly */
error_t gdep103tc2_engine_start(Gdep103tc2Engine* engine, const Gdep103tc2Config* config);

/** Stops the task, powers the panel down and releases everything */
void gdep103tc2_engine_stop(Gdep103tc2Engine* engine);

/** Marks a region of the target as changed */
void gdep103tc2_engine_submit(Gdep103tc2Engine* engine, const Gdep103tc2Region& changed);

/**
 * Flashes the panel black and white to remove ghosting and waits until it is done.
 * @param[in] wipe_content true when the target was cleared by the caller, false to redraw it afterwards
 */
error_t gdep103tc2_engine_flash(Gdep103tc2Engine* engine, bool wipe_content);

/** Powers the supplies down as soon as the panel is idle. */
void gdep103tc2_engine_power_off(Gdep103tc2Engine* engine);
