// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How the tabs are sized */
typedef enum {
    /**
     * The tabs have equal widths and fill the bar, which is as wide as its parent.
     * When a label doesn't fit, the tabs use the small text font. Labels that still don't fit end in an ellipsis.
     */
    LVGL_TABS_MODE_FIXED,
    /** The tabs are as wide as their label, and the bar scrolls horizontally when they don't fit. For many tabs or long labels. */
    LVGL_TABS_MODE_SCROLLABLE,
    /**
     * The bar is as wide as its tabs, and the tabs are as wide as their label, e.g. for small displays.
     * When the bar doesn't fit in its parent, the tabs use the small text font. When it still doesn't fit, the longest labels are shortened and end in an ellipsis.
     * In a flex row, the bar fits in the room that the other widgets of the row leave. Widgets with flex grow don't count, as they take what the bar leaves.
     */
    LVGL_TABS_MODE_WRAP,
} LvglTabsMode;

/**
 * @brief Creates Material 3 tabs: a row of tabs of which one is selected. It only shows the tabs, the app shows the content of the selected tab.
 * The tabs are in LVGL_TABS_MODE_FIXED, see lvgl_tabs_set_mode().
 * Selecting a tab sends LV_EVENT_VALUE_CHANGED to the tabs. The arrow keys move between the tabs (grid navigation).
 * @param[in] parent the parent object for the new tabs
 * @return the created tabs
 */
lv_obj_t* lvgl_tabs_create(lv_obj_t* parent);

/**
 * @brief Adds a tab to the end. The first tab that is added is selected.
 * @param[in] tabs the tabs
 * @param[in] text the label of the tab
 * @return the tab
 */
lv_obj_t* lvgl_tabs_add_tab(lv_obj_t* tabs, const char* text);

/**
 * @brief Adds a tab with an icon above its label, to the end. When the tabs don't fit with the small font and all of them
 * have an icon, they only show their icon.
 * @param[in] tabs the tabs
 * @param[in] icon a symbol of the shared icon font (lvgl_get_shared_icon_default_font()), e.g. LVGL_ICON_SHARED_INFO
 * @param[in] text the label of the tab
 * @return the tab
 */
lv_obj_t* lvgl_tabs_add_tab_with_icon(lv_obj_t* tabs, const char* icon, const char* text);

/**
 * @brief Changes the label of a tab. Use this instead of changing the label widget, so the tabs can size the tab.
 * @param[in] tabs the tabs
 * @param[in] index the index of the tab, ignored when it's out of range
 * @param[in] text the new label
 */
void lvgl_tabs_set_tab_text(lv_obj_t* tabs, uint32_t index, const char* text);

/**
 * @brief Shows or hides a small dot at the end of a tab's label, e.g. for unread messages. The theme determines its colour.
 * It doesn't change the size of the tab.
 * @param[in] tabs the tabs
 * @param[in] index the index of the tab, ignored when it's out of range
 * @param[in] visible true to show the dot
 */
void lvgl_tabs_set_tab_badge(lv_obj_t* tabs, uint32_t index, bool visible);

/**
 * @brief Hides or shows a tab. Hiding the selected tab selects the first tab that is shown, without sending LV_EVENT_VALUE_CHANGED.
 * @param[in] tabs the tabs
 * @param[in] index the index of the tab, ignored when it's out of range. Hidden tabs keep their index.
 * @param[in] hidden true to hide the tab
 */
void lvgl_tabs_set_tab_hidden(lv_obj_t* tabs, uint32_t index, bool hidden);

/**
 * @param[in] tabs the tabs
 * @param[in] mode how the tabs are sized
 */
void lvgl_tabs_set_mode(lv_obj_t* tabs, LvglTabsMode mode);

/**
 * @brief Selects a tab without sending LV_EVENT_VALUE_CHANGED
 * @param[in] tabs the tabs
 * @param[in] index the index of the tab, ignored when it's out of range
 */
void lvgl_tabs_set_selected(lv_obj_t* tabs, uint32_t index);

/**
 * @param[in] tabs the tabs
 * @return the index of the selected tab, 0 when there are no tabs
 */
uint32_t lvgl_tabs_get_selected(lv_obj_t* tabs);

#ifdef __cplusplus
}
#endif
