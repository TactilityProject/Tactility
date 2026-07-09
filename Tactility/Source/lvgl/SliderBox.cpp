#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <Tactility/lvgl/SliderBox.h>

#include <tactility/lvgl_fonts.h>

namespace tt::lvgl {

typedef struct {
    lv_obj_t obj;
    lv_obj_t* minusButton;
    lv_obj_t* slider;
    lv_obj_t* valueLabel;
    lv_obj_t* plusButton;
    int32_t min;
    int32_t max;
    int32_t step;
} SliderBox;

static void sliderbox_constructor(const lv_obj_class_t* classPointer, lv_obj_t* obj);

static lv_obj_class_t sliderbox_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = &sliderbox_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "tt_sliderbox",
    .width_def = LV_PCT(100),
    .height_def = LV_SIZE_CONTENT,
    .editable = false,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(SliderBox),
    .theme_inheritable = false
};

static void sliderbox_constructor(const lv_obj_class_t* classPointer, lv_obj_t* obj) {
    LV_UNUSED(classPointer);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(obj, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 0, LV_STATE_DEFAULT);
}

static void update_value_label(SliderBox* sliderBox) {
    auto value = lv_slider_get_value(sliderBox->slider);
    lv_label_set_text_fmt(sliderBox->valueLabel, "%" LV_PRId32, value);
}

static void update_button_states(SliderBox* sliderBox) {
    auto value = lv_slider_get_value(sliderBox->slider);

    if (value <= sliderBox->min) {
        lv_obj_add_state(sliderBox->minusButton, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(sliderBox->minusButton, LV_STATE_DISABLED);
    }

    if (value >= sliderBox->max) {
        lv_obj_add_state(sliderBox->plusButton, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(sliderBox->plusButton, LV_STATE_DISABLED);
    }
}

static void notify_value_changed(lv_obj_t* obj) {
    lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void on_slider_value_changed(lv_event_t* event) {
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    auto* sliderBox = reinterpret_cast<SliderBox*>(obj);

    auto raw_value = lv_slider_get_value(sliderBox->slider);
    auto steps_from_min = (raw_value - sliderBox->min + sliderBox->step / 2) / sliderBox->step;
    auto snapped_value = sliderBox->min + steps_from_min * sliderBox->step;
    if (snapped_value > sliderBox->max) {
        snapped_value = sliderBox->max;
    }

    if (snapped_value != raw_value) {
        lv_slider_set_value(sliderBox->slider, snapped_value, LV_ANIM_OFF);
    }

    update_value_label(sliderBox);
    update_button_states(sliderBox);
    notify_value_changed(obj);
}

static void on_step_button_clicked(lv_event_t* event, int32_t direction) {
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    auto* sliderBox = reinterpret_cast<SliderBox*>(obj);

    auto value = lv_slider_get_value(sliderBox->slider);
    auto new_value = value + direction * sliderBox->step;
    if (new_value < sliderBox->min) {
        new_value = sliderBox->min;
    } else if (new_value > sliderBox->max) {
        new_value = sliderBox->max;
    }

    if (new_value != value) {
        lv_slider_set_value(sliderBox->slider, new_value, LV_ANIM_ON);
        update_value_label(sliderBox);
        notify_value_changed(obj);
    }

    update_button_states(sliderBox);
}

static void on_minus_button_clicked(lv_event_t* event) {
    on_step_button_clicked(event, -1);
}

static void on_plus_button_clicked(lv_event_t* event) {
    on_step_button_clicked(event, 1);
}

static lv_obj_t* create_step_button(lv_obj_t* parent, const char* symbol, lv_event_cb_t callback, void* userData, uint32_t size) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_pad_all(button, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, userData);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);

    return button;
}

lv_obj_t* sliderbox_create(lv_obj_t* parent, int32_t min, int32_t max, int32_t step, int32_t value) {
    if (max <= min) {
        max = min + 1;
    }
    if (step <= 0) {
        step = 1;
    }

    lv_obj_t* obj = lv_obj_class_create_obj(&sliderbox_class, parent);
    lv_obj_class_init_obj(obj);

    auto* sliderBox = reinterpret_cast<SliderBox*>(obj);
    sliderBox->min = min;
    sliderBox->max = max;
    sliderBox->step = step;

    auto button_size = lvgl_get_text_font_height(FONT_SIZE_DEFAULT) + 8;

    sliderBox->minusButton = create_step_button(obj, LV_SYMBOL_MINUS, &on_minus_button_clicked, obj, button_size);

    sliderBox->slider = lv_slider_create(obj);
    lv_obj_remove_flag(sliderBox->slider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(sliderBox->slider, 1);
    lv_slider_set_range(sliderBox->slider, min, max);
    lv_obj_add_event_cb(sliderBox->slider, &on_slider_value_changed, LV_EVENT_VALUE_CHANGED, obj);

    sliderBox->valueLabel = lv_label_create(obj);
    lv_obj_set_style_text_align(sliderBox->valueLabel, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
    lv_label_set_long_mode(sliderBox->valueLabel, LV_LABEL_LONG_MODE_CLIP);

    // Reserve enough width for the largest value so the layout doesn't jump around.
    // +4 padding wasn't enough margin for 3-digit values to never wrap; widened to
    // a flat per-character estimate instead of trusting raw glyph width too tightly.
    char buffer[16];
    lv_snprintf(buffer, sizeof(buffer), "%" LV_PRId32, min);
    auto width_min = lv_text_get_width(buffer, lv_strlen(buffer), lv_obj_get_style_text_font(sliderBox->valueLabel, LV_PART_MAIN), 0);
    lv_snprintf(buffer, sizeof(buffer), "%" LV_PRId32, max);
    auto width_max = lv_text_get_width(buffer, lv_strlen(buffer), lv_obj_get_style_text_font(sliderBox->valueLabel, LV_PART_MAIN), 0);
    auto max_width = (width_min > width_max) ? width_min : width_max;
    lv_obj_set_width(sliderBox->valueLabel, max_width + 8);

    sliderBox->plusButton = create_step_button(obj, LV_SYMBOL_PLUS, &on_plus_button_clicked, obj, button_size);

    sliderbox_set_value(obj, value, LV_ANIM_OFF);

    return obj;
}

int32_t sliderbox_get_value(lv_obj_t* obj) {
    auto* sliderBox = reinterpret_cast<SliderBox*>(obj);
    return lv_slider_get_value(sliderBox->slider);
}

void sliderbox_set_value(lv_obj_t* obj, int32_t value, lv_anim_enable_t anim) {
    auto* sliderBox = reinterpret_cast<SliderBox*>(obj);

    if (value < sliderBox->min) {
        value = sliderBox->min;
    } else if (value > sliderBox->max) {
        value = sliderBox->max;
    }

    lv_slider_set_value(sliderBox->slider, value, anim);
    update_value_label(sliderBox);
    update_button_states(sliderBox);
}

void sliderbox_add_value_changed_cb(lv_obj_t* obj, lv_event_cb_t callback, void* userData) {
    lv_obj_add_event_cb(obj, callback, LV_EVENT_VALUE_CHANGED, userData);
}

} // namespace tt::lvgl
