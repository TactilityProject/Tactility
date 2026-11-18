// SPDX-License-Identifier: Apache-2.0
#pragma once

/** Called by the window manager after a window's widgets were built, with the LVGL lock held */
void system_bars_window_built();

/** Called by the window manager before a window's widgets are deleted, with the LVGL lock held */
void system_bars_window_deleted();

/** Releases the root layout before it is deleted, with the LVGL lock held */
void system_bars_detach();
