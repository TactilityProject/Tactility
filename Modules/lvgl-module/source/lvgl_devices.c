#include "tactility/lvgl_module.h"


#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/pointer.h>
#include <tactility/log.h>
#include <tactility/lvgl_display.h>
#include <tactility/lvgl_keyboard.h>
#include <tactility/lvgl_pointer.h>

#include <lvgl.h>

#define TAG "lvgl"

void lvgl_devices_attach() {
    lvgl_lock();

    lv_disp_t* lvgl_display = NULL;
    struct Device* kernel_display_device = NULL;
    device_get_first_by_type(&DISPLAY_TYPE, &kernel_display_device);

    // Placeholder drivers (boards not yet migrated to the kernel display driver) register with a
    // NULL api: they exist so the devicetree node resolves, but have nothing for LVGL to bind to.
    if (kernel_display_device != NULL && device_get_driver(kernel_display_device)->api == NULL) {
        device_put(kernel_display_device);
        kernel_display_device = NULL;
    }

    if (kernel_display_device != NULL) {
        // A full-frame buffer (buffer_height=0) needs one contiguous allocation of
        // hres*vres*bpp bytes. Plain ESP32 boards without PSRAM only have ~100-150KB
        // contiguous internal RAM, which a 240x320+ panel can already exceed - so
        // lvgl_display_add() would fail with ERROR_OUT_OF_MEMORY and never register a
        // display. Match the old deprecated-HAL drivers' convention of a partial buffer
        // sized to 1/10th of the vertical resolution; it fits comfortably everywhere,
        // including boards with PSRAM that could afford full-frame.
        uint16_t vres = display_get_resolution_y(kernel_display_device);
        struct LvglDisplayConfig lvgl_display_config = {
            .buffer_height = vres > 10 ? vres / 10 : vres
        };
        if (lvgl_display_add(kernel_display_device, &lvgl_display_config, &lvgl_display) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_display_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_display_device->name);
        }
        device_put(kernel_display_device);
    }

    struct Device* kernel_pointer_device = NULL;
    device_get_first_by_type(&POINTER_TYPE, &kernel_pointer_device);
    // Same placeholder situation as the display above.
    if (kernel_pointer_device != NULL && device_get_driver(kernel_pointer_device)->api == NULL) {
        device_put(kernel_pointer_device);
        kernel_pointer_device = NULL;
    }
    if (kernel_pointer_device != NULL) {
        lv_indev_t* lvgl_pointer_device;
        if (lvgl_pointer_add(kernel_pointer_device, lvgl_display, &lvgl_pointer_device) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_pointer_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVG", kernel_pointer_device->name);
        }
        device_put(kernel_pointer_device);
    }

    struct Device* kernel_keyboard_device = NULL;
    device_get_first_by_type(&KEYBOARD_TYPE, &kernel_keyboard_device);
    lv_indev_t* lvgl_keyboard_device;
    if (kernel_keyboard_device != NULL) {
        if (lvgl_keyboard_add(kernel_keyboard_device, lvgl_display, &lvgl_keyboard_device) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_keyboard_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_keyboard_device->name);
        }
        device_put(kernel_keyboard_device);
    }

    lvgl_unlock();
}

void lvgl_devices_detach() {
    lvgl_lock();

    lv_indev_t* device = lv_indev_get_next(NULL);
    while (device != NULL) {
        lv_indev_type_t type = lv_indev_get_type(device);
        if (type == LV_INDEV_TYPE_POINTER) {
            lvgl_pointer_remove(device);
        } else if (type == LV_INDEV_TYPE_KEYPAD) {
            lvgl_keyboard_remove(device);
        } else {
            lv_indev_delete(device);
        }
        // Always get the first item, because getting the next one doesn't work as the current pointer just became corrupt
        device = lv_indev_get_next(NULL);
    }

    lv_disp_t* display = lv_disp_get_next(NULL);
    while (display != NULL) {
        lv_display_delete(display);
        display = lv_disp_get_next(NULL);
    }

    lvgl_lock();
}
