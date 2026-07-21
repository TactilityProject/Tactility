#pragma once

#include <Tactility/Lock.h>

#include <memory>

namespace tt::lvgl {

constexpr TickType_t defaultLockTime = 500 / portTICK_PERIOD_MS;

/**
 * LVGL locking function
 * @param[in] timeout as ticks
 * @warning when passing zero, we wait forever, as this is the default behaviour for esp_lvgl_port, and we want it to remain consistent
 * @deprecated Use lvgl_lock() or lvgl_try_lock() from lvgl-module instead.
 */
bool lock(TickType_t timeout = portMAX_DELAY);

/** @deprecated Use lvgl_unlock() from lvgl-module instead. */
void unlock();

std::shared_ptr<Lock> getSyncLock();

} // namespace
