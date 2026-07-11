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
    struct Device* kernel_display_device = device_find_first_by_type(&DISPLAY_TYPE);
    // Placeholder drivers (boards not yet migrated to the kernel display driver) register with a
    // NULL api: they exist so the devicetree node resolves, but have nothing for LVGL to bind to.
    if (kernel_display_device != NULL && device_get_driver(kernel_display_device)->api == NULL) {
        kernel_display_device = NULL;
    }
    if (kernel_display_device != NULL) {
        struct LvglDisplayConfig lvgl_display_config = {};
        if (lvgl_display_add(kernel_display_device, &lvgl_display_config, &lvgl_display) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_display_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_display_device->name);
        }
    }

    struct Device* kernel_pointer_device = device_find_first_by_type(&POINTER_TYPE);
    // Same placeholder situation as the display above.
    if (kernel_pointer_device != NULL && device_get_driver(kernel_pointer_device)->api == NULL) {
        kernel_pointer_device = NULL;
    }
    lv_indev_t* lvgl_pointer_device;
    if (kernel_pointer_device != NULL) {
        if (lvgl_pointer_add(kernel_pointer_device, lvgl_display, &lvgl_pointer_device) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_pointer_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVG", kernel_pointer_device->name);
        }
    }

    struct Device* kernel_keyboard_device = device_find_first_by_type(&KEYBOARD_TYPE);
    lv_indev_t* lvgl_keyboard_device;
    if (kernel_keyboard_device != NULL) {
        if (lvgl_keyboard_add(kernel_keyboard_device, lvgl_display, &lvgl_keyboard_device) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_keyboard_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_keyboard_device->name);
        }
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
