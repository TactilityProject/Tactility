#include <lvgl/lvgl.h>
#include <tactility/device.h>
#include <tactility/drivers/led_strip.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <Tactility/Tactility.h>
#include <Tactility/settings/LedStripSettings.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>
#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>

#include <cstdint>
#include <string>

namespace tt::app::ledstripsettings {

extern const ::AppManifest manifest;

constexpr auto* TAG = "LedStripSettings";
constexpr uint8_t CUSTOM_PRESET = 12;
constexpr const char* COLOR_NAMES[] = {
    "White", "Green", "Red", "Blue",
    "Yellow", "Orange", "Cyan", "Magenta",
    "Purple", "Pink", "Teal", "Black"
};

namespace {

struct Context {
    uint32_t appInstanceId = 0;
    Device* device = nullptr;
    settings::ledstrip::LedStripSettings settings;
    bool updated = false;
    enum class Page : uint8_t { Main, Choices, CustomColor } page = Page::Main;
    enum class ChoiceKind : uint8_t { None, Pattern, PrimaryColor, SecondaryColor } choiceKind = ChoiceKind::None;
    bool editingSecondary = false;
    lv_obj_t* toolbar = nullptr;
    lv_obj_t* mainPanel = nullptr;
    lv_obj_t* choicePanel = nullptr;
    lv_obj_t* choiceList = nullptr;
    lv_obj_t* customPanel = nullptr;
    lv_obj_t* returnFocus = nullptr;
    lv_obj_t* modeValue = nullptr;
    lv_obj_t* primaryValue = nullptr;
    lv_obj_t* secondaryRow = nullptr;
    lv_obj_t* secondaryValue = nullptr;
    lv_obj_t* primaryEditor = nullptr;
    lv_obj_t* secondaryEditor = nullptr;
    lv_obj_t* primarySliderBoxes[3] = {};
    lv_obj_t* secondarySliderBoxes[3] = {};
    lv_obj_t* brightnessSlider = nullptr;
    lv_obj_t* brightnessValue = nullptr;
};

const char* patternName(settings::ledstrip::Pattern pattern) {
    switch (pattern) {
        case settings::ledstrip::Pattern::Solid: return "Solid";
        case settings::ledstrip::Pattern::Alternating: return "Alternating";
        case settings::ledstrip::Pattern::Gradient: return "Gradient";
    }
    return "Solid";
}

const char* presetName(uint8_t preset) {
    if (preset == CUSTOM_PRESET) return "Custom";
    return COLOR_NAMES[preset < CUSTOM_PRESET ? preset : 0];
}

void applyAndLog(Context& ctx) {
    const error_t result = settings::ledstrip::apply(ctx.device, ctx.settings);
    if (result != ERROR_NONE) {
        LOG_W(TAG, "Failed to apply LED strip settings: %d", result);
    }
}

void updateVisibility(Context& ctx) {
    const bool hasSecondary = ctx.settings.pattern != settings::ledstrip::Pattern::Solid;
    if (hasSecondary) {
        lv_obj_remove_flag(ctx.secondaryRow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx.secondaryRow, LV_OBJ_FLAG_HIDDEN);
    }
}

void onNavigationPressed(lv_event_t* event);

void returnToMain(Context& ctx) {
    ctx.page = Context::Page::Main;
    ctx.choiceKind = Context::ChoiceKind::None;
    lv_obj_add_flag(ctx.choicePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx.customPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ctx.mainPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(ctx.mainPanel);
    lvgl_toolbar_set_title(ctx.toolbar, "LED Strip");
    lvgl_toolbar_set_nav_action(ctx.toolbar, LV_SYMBOL_CLOSE, onNavigationPressed, &ctx);
    if (ctx.returnFocus != nullptr) {
        lv_group_focus_obj(ctx.returnFocus);
    }
}

void onNavigationPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->page != Context::Page::Main) {
        returnToMain(*ctx);
    } else {
        app_event_emit_close(ctx->appInstanceId);
    }
}

void openCustomEditor(Context& ctx, bool secondary) {
    ctx.page = Context::Page::CustomColor;
    ctx.choiceKind = Context::ChoiceKind::None;
    ctx.editingSecondary = secondary;
    lv_obj_add_flag(ctx.choicePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx.mainPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ctx.customPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(secondary ? ctx.primaryEditor : ctx.secondaryEditor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(secondary ? ctx.secondaryEditor : ctx.primaryEditor, LV_OBJ_FLAG_HIDDEN);
    lvgl_toolbar_set_title(ctx.toolbar, secondary ? "Custom second color" : "Custom color");
    lvgl_toolbar_set_nav_action(ctx.toolbar, LV_SYMBOL_LEFT, onNavigationPressed, &ctx);
    lv_obj_update_layout(ctx.customPanel);
    auto* sliderBox = secondary ? ctx.secondarySliderBoxes[0] : ctx.primarySliderBoxes[0];
    lv_group_focus_obj(sliderBox);
}

size_t choiceCount(Context::ChoiceKind kind) {
    return kind == Context::ChoiceKind::Pattern ? 3 : CUSTOM_PRESET + 1;
}

uint32_t currentChoice(const Context& ctx) {
    switch (ctx.choiceKind) {
        case Context::ChoiceKind::Pattern: return static_cast<uint32_t>(ctx.settings.pattern);
        case Context::ChoiceKind::PrimaryColor: return ctx.settings.primaryPreset;
        case Context::ChoiceKind::SecondaryColor: return ctx.settings.secondaryPreset;
        case Context::ChoiceKind::None: return 0;
    }
    return 0;
}

const char* choiceTitle(Context::ChoiceKind kind) {
    switch (kind) {
        case Context::ChoiceKind::Pattern: return "Choose mode";
        case Context::ChoiceKind::PrimaryColor: return "Choose color";
        case Context::ChoiceKind::SecondaryColor: return "Choose second color";
        case Context::ChoiceKind::None: return "LED Strip";
    }
    return "LED Strip";
}

const char* choiceName(Context::ChoiceKind kind, size_t index) {
    if (kind == Context::ChoiceKind::Pattern) {
        return patternName(static_cast<settings::ledstrip::Pattern>(index));
    }
    return index == CUSTOM_PRESET ? "Custom" : COLOR_NAMES[index];
}

void onChoiceSelected(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const size_t selected = lv_obj_get_index(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    if (selected >= choiceCount(ctx->choiceKind)) return;

    const auto choiceKind = ctx->choiceKind;
    switch (ctx->choiceKind) {
        case Context::ChoiceKind::Pattern:
            ctx->settings.pattern = static_cast<settings::ledstrip::Pattern>(selected);
            break;
        case Context::ChoiceKind::PrimaryColor:
            ctx->settings.primaryPreset = static_cast<uint8_t>(selected);
            break;
        case Context::ChoiceKind::SecondaryColor:
            ctx->settings.secondaryPreset = static_cast<uint8_t>(selected);
            break;
        case Context::ChoiceKind::None:
            return;
    }

    ctx->updated = true;
    lv_label_set_text(ctx->modeValue, patternName(ctx->settings.pattern));
    lv_label_set_text(ctx->primaryValue, presetName(ctx->settings.primaryPreset));
    lv_label_set_text(ctx->secondaryValue, presetName(ctx->settings.secondaryPreset));
    updateVisibility(*ctx);
    if (selected == CUSTOM_PRESET && choiceKind != Context::ChoiceKind::Pattern) {
        openCustomEditor(*ctx, choiceKind == Context::ChoiceKind::SecondaryColor);
        applyAndLog(*ctx);
        return;
    }
    returnToMain(*ctx);
    applyAndLog(*ctx);
}

void openChoice(Context& ctx, Context::ChoiceKind kind, lv_obj_t* returnFocus) {
    ctx.page = Context::Page::Choices;
    ctx.choiceKind = kind;
    ctx.returnFocus = returnFocus;
    lv_obj_clean(ctx.choiceList);

    const size_t selected = currentChoice(ctx);
    lv_obj_t* selectedButton = nullptr;
    for (size_t index = 0; index < choiceCount(kind); index++) {
        auto* button = lv_list_add_button(ctx.choiceList, index == selected ? LV_SYMBOL_OK : nullptr, choiceName(kind, index));
        lv_obj_add_event_cb(button, onChoiceSelected, LV_EVENT_SHORT_CLICKED, &ctx);
        if (index == selected) selectedButton = button;
    }

    lvgl_toolbar_set_title(ctx.toolbar, choiceTitle(kind));
    lvgl_toolbar_set_nav_action(ctx.toolbar, LV_SYMBOL_LEFT, onNavigationPressed, &ctx);
    lv_obj_add_flag(ctx.mainPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ctx.choicePanel, LV_OBJ_FLAG_HIDDEN);
    if (selectedButton != nullptr) {
        lv_group_focus_obj(selectedButton);
    }
}

void onOutputChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->settings.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ctx->updated = true;
    applyAndLog(*ctx);
}

void onPatternPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    openChoice(*ctx, Context::ChoiceKind::Pattern, static_cast<lv_obj_t*>(lv_event_get_target(event)));
}

void onPrimaryColorPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    openChoice(*ctx, Context::ChoiceKind::PrimaryColor, static_cast<lv_obj_t*>(lv_event_get_target(event)));
}

void onSecondaryColorPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    openChoice(*ctx, Context::ChoiceKind::SecondaryColor, static_cast<lv_obj_t*>(lv_event_get_target(event)));
}

void setChannel(LedRgb& color, size_t channel, uint8_t value) {
    switch (channel) {
        case 0: color.r = value; break;
        case 1: color.g = value; break;
        case 2: color.b = value; break;
        default: break;
    }
}

void onColorSliderChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const uint8_t value = static_cast<uint8_t>(lvgl_sliderbox_get_value(target));

    for (size_t channel = 0; channel < 3; channel++) {
        if (target == ctx->primarySliderBoxes[channel]) {
            setChannel(ctx->settings.primaryCustom, channel, value);
            ctx->updated = true;
            applyAndLog(*ctx);
            return;
        }
        if (target == ctx->secondarySliderBoxes[channel]) {
            setChannel(ctx->settings.secondaryCustom, channel, value);
            ctx->updated = true;
            applyAndLog(*ctx);
            return;
        }
    }
}

void onBrightnessChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const auto value = static_cast<uint8_t>(lvgl_sliderbox_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    ctx->settings.brightness = value;
    lv_label_set_text_fmt(ctx->brightnessValue, "%u%%", (static_cast<unsigned>(value) * 100 + 127) / 255);
    ctx->updated = true;
    applyAndLog(*ctx);
}

lv_obj_t* createRow(lv_obj_t* parent, const char* title) {
    auto* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    auto* label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

void createRgbEditor(lv_obj_t* parent, const char* title, const LedRgb& color, lv_obj_t** sliderBoxes, Context* ctx, bool primary) {
    auto* editor = lv_obj_create(parent);
    lv_obj_set_width(editor, LV_PCT(100));
    lv_obj_set_flex_flow(editor, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(editor, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(editor, 0, LV_STATE_DEFAULT);
    auto* heading = lv_label_create(editor);
    lv_label_set_text(heading, title);

    constexpr const char* CHANNELS[] = { "R", "G", "B" };
    const uint8_t channels[] = { color.r, color.g, color.b };
    for (size_t i = 0; i < 3; i++) {
        auto* row = lv_obj_create(editor);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 1, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
        auto* label = lv_label_create(row);
        lv_label_set_text(label, CHANNELS[i]);
        lv_obj_set_width(label, LV_PCT(12));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        sliderBoxes[i] = lvgl_sliderbox_create(row, 0, UINT8_MAX, 8, channels[i]);
        lv_obj_set_width(sliderBoxes[i], LV_PCT(84));
        lv_obj_align(sliderBoxes[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lvgl_sliderbox_add_value_changed_cb(sliderBoxes[i], onColorSliderChanged, ctx);
    }
    if (primary) {
        ctx->primaryEditor = editor;
    } else {
        ctx->secondaryEditor = editor;
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    ctx->toolbar = lvgl_toolbar_create(parent, "LED Strip");
    lvgl_toolbar_set_nav_action(ctx->toolbar, LV_SYMBOL_CLOSE, onNavigationPressed, ctx);

    ctx->mainPanel = lv_obj_create(parent);
    lv_obj_set_width(ctx->mainPanel, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->mainPanel, 1);
    lv_obj_set_flex_flow(ctx->mainPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->mainPanel, 2, LV_STATE_DEFAULT);
    lv_obj_add_flag(ctx->mainPanel, LV_OBJ_FLAG_SCROLLABLE);

    ctx->customPanel = lv_obj_create(parent);
    lv_obj_set_width(ctx->customPanel, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->customPanel, 1);
    lv_obj_set_flex_flow(ctx->customPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(ctx->customPanel, LV_OBJ_FLAG_HIDDEN);

    auto* outputRow = createRow(ctx->mainPanel, "Output");
    auto* outputSwitch = lv_switch_create(outputRow);
    if (ctx->settings.enabled) lv_obj_add_state(outputSwitch, LV_STATE_CHECKED);
    lv_obj_align(outputSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(outputSwitch, onOutputChanged, LV_EVENT_VALUE_CHANGED, ctx);

    auto* modeRow = createRow(ctx->mainPanel, "Mode");
    auto* modeButton = lv_button_create(modeRow);
    lv_obj_set_width(modeButton, LV_PCT(62));
    lv_obj_align(modeButton, LV_ALIGN_RIGHT_MID, 0, 0);
    ctx->modeValue = lv_label_create(modeButton);
    lv_label_set_text(ctx->modeValue, patternName(ctx->settings.pattern));
    lv_obj_center(ctx->modeValue);
    lv_obj_add_event_cb(modeButton, onPatternPressed, LV_EVENT_SHORT_CLICKED, ctx);

    auto* primaryRow = createRow(ctx->mainPanel, "Color");
    auto* primaryButton = lv_button_create(primaryRow);
    lv_obj_set_width(primaryButton, LV_PCT(62));
    lv_obj_align(primaryButton, LV_ALIGN_RIGHT_MID, 0, 0);
    ctx->primaryValue = lv_label_create(primaryButton);
    lv_label_set_text(ctx->primaryValue, presetName(ctx->settings.primaryPreset));
    lv_obj_center(ctx->primaryValue);
    lv_obj_add_event_cb(primaryButton, onPrimaryColorPressed, LV_EVENT_SHORT_CLICKED, ctx);

    createRgbEditor(ctx->customPanel, "Red / Green / Blue", ctx->settings.primaryCustom, ctx->primarySliderBoxes, ctx, true);
    lv_obj_add_flag(ctx->primaryEditor, LV_OBJ_FLAG_HIDDEN);

    ctx->secondaryRow = createRow(ctx->mainPanel, "Second color");
    auto* secondaryButton = lv_button_create(ctx->secondaryRow);
    lv_obj_set_width(secondaryButton, LV_PCT(62));
    lv_obj_align(secondaryButton, LV_ALIGN_RIGHT_MID, 0, 0);
    ctx->secondaryValue = lv_label_create(secondaryButton);
    lv_label_set_text(ctx->secondaryValue, presetName(ctx->settings.secondaryPreset));
    lv_obj_center(ctx->secondaryValue);
    lv_obj_add_event_cb(secondaryButton, onSecondaryColorPressed, LV_EVENT_SHORT_CLICKED, ctx);

    createRgbEditor(ctx->customPanel, "Red / Green / Blue", ctx->settings.secondaryCustom, ctx->secondarySliderBoxes, ctx, false);
    lv_obj_add_flag(ctx->secondaryEditor, LV_OBJ_FLAG_HIDDEN);

    auto* brightnessRow = createRow(ctx->mainPanel, "Brightness");
    ctx->brightnessValue = lv_label_create(brightnessRow);
    lv_label_set_text_fmt(ctx->brightnessValue, "%u%%", (static_cast<unsigned>(ctx->settings.brightness) * 100 + 127) / 255);
    lv_obj_align(ctx->brightnessValue, LV_ALIGN_RIGHT_MID, 0, 0);
    ctx->brightnessSlider = lvgl_sliderbox_create(ctx->mainPanel, 0, UINT8_MAX, 17, ctx->settings.brightness);
    lv_obj_set_width(ctx->brightnessSlider, LV_PCT(100));
    lvgl_sliderbox_add_value_changed_cb(ctx->brightnessSlider, onBrightnessChanged, ctx);

    ctx->choicePanel = lv_obj_create(parent);
    lv_obj_set_width(ctx->choicePanel, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->choicePanel, 1);
    lv_obj_set_flex_flow(ctx->choicePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(ctx->choicePanel, LV_OBJ_FLAG_HIDDEN);
    ctx->choiceList = lv_list_create(ctx->choicePanel);
    lv_obj_set_width(ctx->choiceList, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->choiceList, 1);

    updateVisibility(*ctx);
}

void persistIfUpdated(const Context& ctx) {
    if (!ctx.updated) return;
    const auto settingsToSave = ctx.settings;
    const std::string deviceName = ctx.device->name;
    getMainDispatcher().dispatch([settingsToSave, deviceName] {
        if (!settings::ledstrip::save(deviceName.c_str(), settingsToSave)) {
            LOG_W(TAG, "Failed to save LED strip settings");
        }
    });
}

int32_t appMain(int argc, char* argv[]) {
    Device* device = nullptr;
    if (device_get_first_by_type(&LED_STRIP_TYPE, &device) != ERROR_NONE) {
        return 0;
    }

    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();
    ctx.device = device;
    ctx.settings = settings::ledstrip::loadOrGetDefault(device->name);
    uint16_t ledCount = 0;
    if (led_strip_get_length(device, &ledCount) != ERROR_NONE || ledCount == 0) {
        device_put(device);
        return 0;
    }
    applyAndLog(ctx);

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    AppEventSubscription subscription {};
    if (app_event_subscribe(&subscription, &eventGroup) != ERROR_NONE) {
        task_event_group_destruct(&eventGroup);
        device_put(device);
        return 0;
    }

    const WindowId window = window_manager_create(ctx.appInstanceId, createWidgets, &ctx);
    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&eventGroup, nullptr, portMAX_DELAY);
        AppEvent event {};
        while (app_event_poll(&subscription, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                persistIfUpdated(ctx);
                shouldClose = true;
                break;
            }
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&subscription);
    task_event_group_destruct(&eventGroup);
    device_put(device);
    return 0;
}

}

extern const ::AppManifest manifest = {
    .id = "tactility.ledstripsettings",
    .name = "LED Strip",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

}