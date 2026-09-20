#pragma once

// Applies landscape rotation while the keyboard accessory ("keyboard0") is started, restoring the
// prior rotation on stop - unless the user changed it manually since attaching, in which case
// their choice is respected. Start/stop are now driven by the kernel's hotplug poller (see
// tab5_keyboard.cpp's Driver::probe), not polled here. Called from module.cpp's start()/stop().
void tab5_keyboard_lvgl_rotation_start();
void tab5_keyboard_lvgl_rotation_stop();
