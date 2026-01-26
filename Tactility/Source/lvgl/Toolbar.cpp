#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <Tactility/lvgl/Toolbar.h>

#include <Tactility/Check.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/lvgl/Spinner.h>

namespace tt::lvgl {

static int getToolbarHeight(hal::UiScale uiScale) {
    if (uiScale == hal::UiScale::Smallest) {
        return 22;
    } else {
        return 40;
    }
}

static const _lv_font_t* getToolbarFont(hal::UiScale uiScale) {
    if (uiScale == hal::UiScale::Smallest) {
        return &lv_font_montserrat_14;
    } else {
        return &lv_font_montserrat_18;
    }
}

/**
 * Helps with button expansion and also with vertical alignment of content,
 * as the parent flex doesn't allow for vertical alignment
 */
static lv_obj_t* create_action_wrapper(lv_obj_t* parent) {
    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, LV_SIZE_CONTENT, getToolbarHeight(hal::getConfiguration()->uiScale));
    lv_obj_set_style_pad_all(wrapper, 2, LV_STATE_DEFAULT); // For selection / click expansion
    lv_obj_set_style_bg_opa(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(wrapper, 0, LV_STATE_DEFAULT);
    return wrapper;
}

typedef struct {
    lv_obj_t obj;
    lv_obj_t* title_label;
    lv_obj_t* close_button;
    lv_obj_t* close_button_image;
    lv_obj_t* action_container;
    uint8_t  action_count;
} Toolbar;

static void toolbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);

static const lv_obj_class_t toolbar_class = {
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

static void stop_app(lv_event_t* event) {
    app::stop();
}

static void toolbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    LV_TRACE_OBJ_CREATE("finished");
}

lv_obj_t* toolbar_create(lv_obj_t* parent, const std::string& title) {
    LV_LOG_INFO("begin");
    auto ui_scale = hal::getConfiguration()->uiScale;
    auto toolbar_height = getToolbarHeight(ui_scale);
    lv_obj_t* obj = lv_obj_class_create_obj(&toolbar_class, parent);
    lv_obj_class_init_obj(obj);
    lv_obj_set_height(obj, toolbar_height);

    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

    lv_obj_center(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);

    auto* close_button_wrapper = lv_obj_create(obj);
    lv_obj_set_size(close_button_wrapper, LV_SIZE_CONTENT, toolbar_height);
    lv_obj_set_style_pad_all(close_button_wrapper, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(close_button_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(close_button_wrapper, 0, LV_STATE_DEFAULT);

    toolbar->close_button = lv_button_create(close_button_wrapper);

    if (ui_scale == hal::UiScale::Smallest) {
        lv_obj_set_size(toolbar->close_button, toolbar_height - 8, toolbar_height - 8);
    } else {
        lv_obj_set_size(toolbar->close_button, toolbar_height - 6, toolbar_height - 6);
    }

    lv_obj_set_style_pad_all(toolbar->close_button, 0, LV_STATE_DEFAULT);
    lv_obj_align(toolbar->close_button, LV_ALIGN_CENTER, 0, 0);
    toolbar->close_button_image = lv_image_create(toolbar->close_button);
    lv_obj_align(toolbar->close_button_image, LV_ALIGN_CENTER, 0, 0);

    auto* title_wrapper = lv_obj_create(obj);
    lv_obj_set_size(title_wrapper, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_style_bg_opa(title_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(title_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(title_wrapper, 1);

    if (ui_scale == hal::UiScale::Smallest) {
        lv_obj_set_style_pad_left(title_wrapper, 4, LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_pad_left(title_wrapper, 8, LV_STATE_DEFAULT);
    }

    toolbar->title_label = lv_label_create(title_wrapper);
    lv_obj_set_style_text_font(toolbar->title_label, getToolbarFont(ui_scale), LV_STATE_DEFAULT);
    lv_label_set_text(toolbar->title_label, title.c_str());
    lv_label_set_long_mode(toolbar->title_label, LV_LABEL_LONG_MODE_SCROLL);
    lv_obj_set_style_text_align(toolbar->title_label, LV_TEXT_ALIGN_LEFT, LV_STATE_DEFAULT);
    lv_obj_align(toolbar->title_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_width(toolbar->title_label, LV_PCT(100));

    toolbar->action_container = lv_obj_create(obj);
    lv_obj_set_width(toolbar->action_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(toolbar->action_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(toolbar->action_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(toolbar->action_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(toolbar->action_container, 0, LV_STATE_DEFAULT);

    toolbar_set_nav_action(obj, LV_SYMBOL_CLOSE, &stop_app, nullptr);

    // If we don't have a touch device, we assume there's some other kind of input like a keyboard, an encoder or button control
    // In that scenario we want to automatically have the close button selected so the user doesn't have to press the widget selection
    // an extra time for every screen.
    if (!hal::hasDevice(hal::Device::Type::Touch)) {
        lv_group_focus_obj(toolbar->close_button);
    }

    return obj;
}

lv_obj_t* toolbar_create(lv_obj_t* parent, const app::AppContext& app) {
    return toolbar_create(parent, app.getManifest().appName);
}

void toolbar_set_title(lv_obj_t* obj, const std::string& title) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    lv_label_set_text(toolbar->title_label, title.c_str());
}

void toolbar_set_nav_action(lv_obj_t* obj, const char* icon, lv_event_cb_t callback, void* user_data) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    lv_obj_add_event_cb(toolbar->close_button, callback, LV_EVENT_SHORT_CLICKED, user_data);
    lv_image_set_src(toolbar->close_button_image, icon); // e.g. LV_SYMBOL_CLOSE
}

lv_obj_t* toolbar_add_button_action(lv_obj_t* obj, const char* imageOrButton, bool isImage, lv_event_cb_t callback, void* user_data) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    check(toolbar->action_count < TOOLBAR_ACTION_LIMIT, "max actions reached");
    toolbar->action_count++;

    auto ui_scale = hal::getConfiguration()->uiScale;
    auto toolbar_height = getToolbarHeight(ui_scale);

    auto* wrapper = create_action_wrapper(toolbar->action_container);

    auto* action_button = lv_button_create(wrapper);
    if (ui_scale == hal::UiScale::Smallest) {
        lv_obj_set_size(action_button, toolbar_height - 8, toolbar_height - 8);
    } else {
        lv_obj_set_size(action_button, toolbar_height - 6, toolbar_height - 6);
    }
    lv_obj_set_style_pad_all(action_button, 0, LV_STATE_DEFAULT);
    lv_obj_align(action_button, LV_ALIGN_CENTER, 0, 0);

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

lv_obj_t* toolbar_add_image_button_action(lv_obj_t* obj, const char* imagePath, lv_event_cb_t callback, void* user_data) {
    return toolbar_add_button_action(obj, imagePath, true, callback, user_data);
}

lv_obj_t* toolbar_add_text_button_action(lv_obj_t* obj, const char* text, lv_event_cb_t callback, void* user_data) {
    return toolbar_add_button_action(obj, text, false, callback, user_data);
}

lv_obj_t* toolbar_add_switch_action(lv_obj_t* obj) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);

    auto* wrapper = create_action_wrapper(toolbar->action_container);
    lv_obj_set_style_pad_hor(wrapper, 4, LV_STATE_DEFAULT);

    lv_obj_t* widget = lv_switch_create(wrapper);
    lv_obj_set_align(widget, LV_ALIGN_CENTER);
    return widget;
}

lv_obj_t* toolbar_add_spinner_action(lv_obj_t* obj) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);

    auto* wrapper = create_action_wrapper(toolbar->action_container);

    auto* spinner = spinner_create(wrapper);
    lv_obj_set_align(spinner, LV_ALIGN_CENTER);
    return spinner;
}

void toolbar_clear_actions(lv_obj_t* obj) {
    auto* toolbar = reinterpret_cast<Toolbar*>(obj);
    lv_obj_clean(toolbar->action_container);
    toolbar->action_count = 0;
}

} // namespace
