// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl_window_manager/system_bars.h>

#include <stdint.h>

/** What a window did with the page bar, kept while a dialog uses the bar */
struct SystemBarsPageState {
    SystemBarsCallback onPrevious;
    SystemBarsCallback onNext;
    void* pageUserData;
    bool previousHidden;
    bool nextHidden;
    bool mainHidden;
    /** Counts the windows that were deleted, to tell whether the state still applies */
    uint32_t generation;
};

/** Saves the state, and hides the page buttons and the main action. Call with the LVGL lock held. */
void system_bars_page_state_take(SystemBarsPageState* saved);

/** Applies a saved state, unless a window was deleted since it was taken. Call with the LVGL lock held. */
void system_bars_page_state_restore(const SystemBarsPageState* saved);

/** Called by the window manager after a window's widgets were built, with the LVGL lock held */
void system_bars_window_built();

/** Called by the window manager before a window's widgets are deleted, with the LVGL lock held */
void system_bars_window_deleted();

/** Releases the root layout before it is deleted, with the LVGL lock held */
void system_bars_detach();
