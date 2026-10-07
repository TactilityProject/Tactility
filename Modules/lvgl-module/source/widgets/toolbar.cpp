// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <lvgl/widgets/toolbar.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/spinner.h>

#include <tactility/check.h>
#include <tactility/drivers/pointer.h>
#include <tactility/log.h>

constexpr auto* TAG = "lvgl_toolbar";

static uint32_t getToolbarHeight(UiDensity uiDensity) {
    if (uiDensity == LVGL_UI_DENSITY_COMPACT) {
        return lvgl_get_text_font_height(FONT_SIZE_DEFAULT) * 1.6f;
    } else {
        return lvgl_get_text_font_height(FONT_SIZE_LARGE) * 2.2f;
    }
}

static const _lv_font_t* getToolbarFont(UiDensity uiDensity) {
    if (uiDensity == LVGL_UI_DENSITY_COMPACT) {
        return lvgl_get_text_font(FONT_SIZE_DEFAULT);
    } else {
        return lvgl_get_text_font(FONT_SIZE_LARGE);
    }
}

/** The size of icon buttons, which leaves room for the theme's focus indicator */
static uint32_t getActionButtonSize(UiDensity uiDensity) {
    auto toolbar_height = getToolbarHeight(uiDensity);
    auto padding = (uiDensity != LVGL_UI_DENSITY_COMPACT) ? (uint32_t)(toolbar_height * 0.2f) : 8;
    return toolbar_height - padding;
}

typedef struct {
    lv_obj_t obj;
    lv_obj_t* title_label;
    lv_obj_t* close_button;
    lv_obj_t* close_button_image;
    uint8_t action_count;
    lv_event_cb_t nav_action_callback;
} Toolbar;

static void toolbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);

// Not static: the theme styles toolbars by this class
extern "C" const lv_obj_class_t lvgl_toolbar_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = &toolbar_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = nullptr,
    .width_def = LV_PCT(100),
    .height_def = LV_SIZE_CONTENT,
    .editable = false,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(Toolbar),
    .theme_inheritable = false
};

static lv_event_cb_t nav_action_callback = nullptr;

void lvgl_toolbar_configure(const ToolbarConfig* config) {
    nav_action_callback = config->nav_action_callback;
}

static void default_nav_action(lv_event_t* event) {
    if (nav_action_callback != nullptr) {
        nav_action_callback(event);
    }
}

static void toolbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
}

static lv_obj_t* create_action_button(lv_obj_t* parent, UiDensity ui_density) {
    auto button_size = getActionButtonSize(ui_density);
    lv_obj_t* button = lvgl_icon_button_create(parent);
    lv_obj_set_size(button, button_size, button_size);
    return button;
}

lv_obj_t* lvgl_toolbar_create(lv_obj_t* parent, const char* title) {
    auto ui_density = lvgl_get_ui_density();
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_toolbar_class, parent);
    lv_obj_class_init_obj(obj);
    lv_obj_set_height(obj, getToolbarHeight(ui_density));

    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    toolbar->nav_action_callback = nullptr;

    lv_obj_center(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // The arrow keys move between the close button and the actions, and up and down leave the toolbar
    lvgl_grid_navigation_add(obj);

    toolbar->close_button = create_action_button(obj, ui_density);
    toolbar->close_button_image = lv_image_create(toolbar->close_button);
    lv_obj_align(toolbar->close_button_image, LV_ALIGN_CENTER, 0, 0);

    toolbar->title_label = lv_label_create(obj);
    lv_obj_set_style_text_font(toolbar->title_label, getToolbarFont(ui_density), LV_STATE_DEFAULT);
    lv_label_set_text(toolbar->title_label, title);
    lv_label_set_long_mode(toolbar->title_label, LV_LABEL_LONG_MODE_SCROLL);
    lv_obj_set_flex_grow(toolbar->title_label, 1);

    lvgl_toolbar_set_nav_action(obj, LV_SYMBOL_CLOSE, &default_nav_action, nullptr);

    // If we don't have a touch device, we assume there's some other kind of input like a keyboard, an encoder or button control
    // In that scenario we want to automatically have the close button selected so the user doesn't have to press the widget selection
    // an extra time for every screen.
    if (!device_has_active_by_type(&POINTER_TYPE)) {
        lv_obj_update_layout(obj); // Resolve flex layout first, so focus/state invalidate against final coords
        lv_group_focus_obj(obj);
        lv_gridnav_set_focused(obj, toolbar->close_button, LV_ANIM_OFF);
    }

    return obj;
}

void lvgl_toolbar_set_title(lv_obj_t* obj, const char* title) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    lv_label_set_text(toolbar->title_label, title);
}

void lvgl_toolbar_set_nav_action(lv_obj_t* obj, const char* icon, lv_event_cb_t callback, void* user_data) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    if (toolbar->nav_action_callback != nullptr) {
        lv_obj_remove_event_cb(toolbar->close_button, toolbar->nav_action_callback);
    }
    toolbar->nav_action_callback = callback;
    if (callback != nullptr) {
        lv_obj_add_event_cb(toolbar->close_button, callback, LV_EVENT_SHORT_CLICKED, user_data);
    }
    lv_image_set_src(toolbar->close_button_image, icon); // e.g. LV_SYMBOL_CLOSE
}

static lv_obj_t* toolbar_add_button_action(lv_obj_t* obj, const char* imageOrButton, bool isImage, lv_event_cb_t callback, void* user_data) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    check(toolbar->action_count < TOOLBAR_ACTION_LIMIT, "max actions reached");
    toolbar->action_count++;

    lv_obj_t* action_button = create_action_button(obj, lvgl_get_ui_density());
    lv_obj_add_event_cb(action_button, callback, LV_EVENT_SHORT_CLICKED, user_data);
    lv_obj_t* button_content;
    if (isImage) {
        button_content = lv_image_create(action_button);
        lv_image_set_src(button_content, imageOrButton);
    } else {
        button_content = lv_label_create(action_button);
        lv_label_set_text(button_content, imageOrButton);
    }
    lv_obj_align(button_content, LV_ALIGN_CENTER, 0, 0);

    return action_button;
}

lv_obj_t* lvgl_toolbar_add_image_button_action(lv_obj_t* obj, const char* imagePath, lv_event_cb_t callback, void* user_data) {
    return toolbar_add_button_action(obj, imagePath, true, callback, user_data);
}

lv_obj_t* lvgl_toolbar_add_text_button_action(lv_obj_t* obj, const char* text, lv_event_cb_t callback, void* user_data) {
    return toolbar_add_button_action(obj, text, false, callback, user_data);
}

lv_obj_t* lvgl_toolbar_add_switch_action(lv_obj_t* obj) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    check(toolbar->action_count < TOOLBAR_ACTION_LIMIT, "max actions reached");
    toolbar->action_count++;

    return lv_switch_create(obj);
}

lv_obj_t* lvgl_toolbar_add_dropdown_action(lv_obj_t* obj, const char* options, lv_coord_t width, const char* text) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    check(toolbar->action_count < TOOLBAR_ACTION_LIMIT, "max actions reached");
    toolbar->action_count++;

    // The arrow keys can't both navigate the toolbar and the opened dropdown
    LOG_W(TAG, "Dropdown actions are deprecated: the toolbar can't be navigated with the arrow keys when it has one");
    lvgl_grid_navigation_remove(obj);

    lv_obj_t* widget = lv_dropdown_create(obj);
    lv_dropdown_set_options(widget, options);
    lv_dropdown_set_selected_highlight(widget, false);
    if (width > 0) {
        lv_obj_set_width(widget, width);
    }
    if (text != nullptr) {
        lv_dropdown_set_text(widget, text);
    }

    return widget;
}

lv_obj_t* lvgl_toolbar_add_spinner_action(lv_obj_t* obj) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    check(toolbar->action_count < TOOLBAR_ACTION_LIMIT, "max actions reached");
    toolbar->action_count++;

    auto* spinner = lvgl_spinner_create(obj);

    if (lv_display_get_color_format(lv_obj_get_display(obj)) == LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_image_recolor(spinner, lv_theme_get_color_secondary(obj), LV_STATE_DEFAULT);
        lv_obj_set_style_image_recolor_opa(spinner, LV_OPA_COVER, LV_STATE_DEFAULT);
    }

    return spinner;
}

void lvgl_toolbar_clear_actions(lv_obj_t* obj) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    // The actions follow the close button and the title
    while (lv_obj_get_child_count(obj) > 2) {
        lv_obj_delete(lv_obj_get_child(obj, 2));
    }
    toolbar->action_count = 0;
}
