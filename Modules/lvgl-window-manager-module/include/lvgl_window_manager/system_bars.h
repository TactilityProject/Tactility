// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where the statusbar is placed relative to the apps */
enum SystemBarsLayout {
    /** A bar on top of the apps, and the page bar below them */
    SYSTEM_BARS_LAYOUT_SPLIT,
    /** A vertical strip on the left that holds the statusbar and the page bar */
    SYSTEM_BARS_LAYOUT_SIDE,
};

/** The side of the screen that the system bars align to */
enum SystemBarsAlignment {
    /** The strip is on the left in the side layout, and the page bar's buttons start at the left in the split layout */
    SYSTEM_BARS_ALIGNMENT_LEFT,
    /** The strip is on the right in the side layout, and the page bar's buttons are mirrored and start at the right in the split layout */
    SYSTEM_BARS_ALIGNMENT_RIGHT,
};

struct SystemBarsConfig {
    /** Optional. Opens the system menu on a swipe down from the top edge, in the side layout. */
    void (*show_menu)(void);

    /**
     * Optional. Chooses the layout for a display size. Without it the split layout is used.
     * Circular displays always use the split layout.
     * @warning Called on the LVGL task with the LVGL lock held, whenever the layout is applied.
     */
    enum SystemBarsLayout (*resolve_layout)(int32_t width, int32_t height);

    /**
     * Optional. Chooses the alignment. Without it the system bars align to the left.
     * @warning Called on the LVGL task with the LVGL lock held, whenever the layout is applied.
     */
    enum SystemBarsAlignment (*resolve_alignment)(void);

    /**
     * Optional. Called once the root layout exists, so the owner can add widgets that overlay it.
     * @param[in] root the root widget, which is deleted by window_manager_stop()
     * @warning Called on the LVGL task with the LVGL lock held. Don't call window manager functions from it.
     */
    void (*on_screen_created)(lv_obj_t* root);
};

/**
 * Makes the window manager build the root layout: a statusbar, a container for the app windows
 * and a page bar. Call before window_manager_start().
 */
void system_bars_configure(const struct SystemBarsConfig* config);

/**
 * Applies the layout and the display size again, e.g. after the user changed the layout.
 * @warning Caller must hold the LVGL lock.
 */
void system_bars_refresh(void);

/**
 * Makes a pointer device that was created after the system bars started open the system menu with a swipe.
 * Doesn't read any settings, so it is safe to call from any task that holds the LVGL lock.
 * @param[in] indev a pointer device
 * @warning Caller must hold the LVGL lock.
 */
void system_bars_add_pointer(lv_indev_t* indev);

/**
 * Shows or hides the statusbar, e.g. for a full screen app. A window must show it again when its widgets are destroyed.
 * @warning Caller must hold the LVGL lock.
 */
void system_bars_set_statusbar_visible(bool visible);

/**
 * @warning Caller must hold the LVGL lock.
 * @return the layout that is shown
 */
enum SystemBarsLayout system_bars_get_layout(void);

/**
 * @warning Caller must hold the LVGL lock.
 * @return the alignment that is shown
 */
enum SystemBarsAlignment system_bars_get_alignment(void);

/*
 * The page bar holds the main action button and the page buttons of the topmost window.
 * It is below the apps in the split layout and in the strip in the side layout, and it follows the layout by itself.
 * It is only shown while it has a visible button. Everything a window sets is reset when its widgets are deleted,
 * so set it again each time the widgets are created.
 */

typedef void (*SystemBarsCallback)(void* user_data);

/**
 * Shows the main action button, which is hidden until an app sets it. It's meant for the one action that an app
 * wants within reach, e.g. the launcher's button that switches between its apps and settings.
 * Calling it again changes the icon and the callback.
 * @param[in] icon a shared icon (see lvgl/icons/shared.h), or NULL to hide the button
 * @param[in] on_pressed called when the button is pressed
 * @warning Caller must hold the LVGL lock. The callback runs on the LVGL task with the LVGL lock held.
 */
void system_bars_set_main_action(const char* icon, SystemBarsCallback on_pressed, void* user_data);

/**
 * Sets what the previous and next page buttons do. The buttons are only shown after system_bars_set_page_buttons_visible().
 * @param[in] on_previous called when the previous page button is pressed
 * @param[in] on_next called when the next page button is pressed
 * @warning Caller must hold the LVGL lock. The callbacks run on the LVGL task with the LVGL lock held.
 */
void system_bars_set_page_button_callbacks(SystemBarsCallback on_previous, SystemBarsCallback on_next, void* user_data);

/**
 * In the side layout, a swipe down that starts within about 5 mm of the top edge opens the system menu.
 * @warning Caller must hold the LVGL lock.
 * @return true when the press of the pointer that is being swiped started in that zone, so a swipe down must be left to the system menu
 */
bool system_bars_is_menu_swipe_zone_pressed(void);

/**
 * Shows or hides both page buttons.
 * @warning Caller must hold the LVGL lock.
 */
void system_bars_set_page_buttons_visible(bool visible);

#ifdef __cplusplus
}
#endif
