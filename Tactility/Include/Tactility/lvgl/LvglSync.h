#pragma once

#include <Tactility/Lock.h>

#include <memory>

namespace tt::lvgl {

constexpr TickType_t defaultLockTime = 500 / portTICK_PERIOD_MS;

/**
 * LVGL locking function
 * @param[in] timeout as ticks
 * @warning when passing zero, we wait forever, as this is the default behaviour for esp_lvgl_port, and we want it to remain consistent
 */
bool lock(TickType_t timeout = portMAX_DELAY);

void unlock();

std::shared_ptr<Lock> getSyncLock();

} // namespace
