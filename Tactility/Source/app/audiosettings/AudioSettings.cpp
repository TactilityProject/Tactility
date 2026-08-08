#include <Tactility/Tactility.h>
#include <Tactility/PubSub.h>
#include <Tactility/service/audio/Audio.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::audiosettings {

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    PubSub<service::audio::AudioEvent>::SubscriptionHandle audioSubscription = nullptr;

    lv_obj_t* inputEnabledSwitch = nullptr;
    lv_obj_t* inputMuteSwitch = nullptr;
    lv_obj_t* inputVolumeSlider = nullptr;

    lv_obj_t* outputEnabledSwitch = nullptr;
    lv_obj_t* outputMuteSwitch = nullptr;
    lv_obj_t* outputVolumeSlider = nullptr;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onInputEnabledSwitch(lv_event_t* event) {
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    service::audio::setInputEnabled(enabled);
}

void onOutputEnabledSwitch(lv_event_t* event) {
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    service::audio::setOutputEnabled(enabled);
}

void onInputMuteSwitch(lv_event_t* event) {
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool muted = lv_obj_has_state(sw, LV_STATE_CHECKED);
    service::audio::setInputMuted(muted);
}

void onOutputMuteSwitch(lv_event_t* event) {
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool muted = lv_obj_has_state(sw, LV_STATE_CHECKED);
    service::audio::setOutputMuted(muted);
}

void onInputVolumeSlider(lv_event_t* event) {
    auto* sliderBox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    float percent = static_cast<float>(lvgl_sliderbox_get_value(sliderBox));
    service::audio::setInputVolume(percent);
}

void onOutputVolumeSlider(lv_event_t* event) {
    auto* sliderBox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    float percent = static_cast<float>(lvgl_sliderbox_get_value(sliderBox));
    service::audio::setOutputVolume(percent);
}

lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);

    auto* title_label = lv_label_create(wrapper);
    lv_label_set_text(title_label, title);

    return wrapper;
}

lv_obj_t* createSwitchRow(lv_obj_t* parent, const char* label, lv_event_cb_t cb, void* userData) {
    auto* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);

    auto* row_label = lv_label_create(row);
    lv_label_set_text(row_label, label);
    lv_obj_align(row_label, LV_ALIGN_LEFT_MID, 0, 0);

    auto* sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, userData);

    return sw;
}

lv_obj_t* createSliderRow(lv_obj_t* parent, const char* label, int32_t initialValue, lv_event_cb_t cb, void* userData) {
    auto* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);

    auto* row_label = lv_label_create(row);
    lv_label_set_text(row_label, label);
    lv_obj_align(row_label, LV_ALIGN_LEFT_MID, 0, 0);

    auto* sliderBox = lvgl_sliderbox_create(row, 0, 100, 10, initialValue);
    lv_obj_set_width(sliderBox, LV_PCT(50));
    lv_obj_align(sliderBox, LV_ALIGN_RIGHT_MID, 0, 0);
    lvgl_sliderbox_add_value_changed_cb(sliderBox, cb, userData);

    return sliderBox;
}

void refresh(Context* ctx) {
    if (ctx->inputEnabledSwitch) {
        if (service::audio::isInputEnabled()) lv_obj_add_state(ctx->inputEnabledSwitch, LV_STATE_CHECKED);
        else lv_obj_remove_state(ctx->inputEnabledSwitch, LV_STATE_CHECKED);
    }
    if (ctx->inputMuteSwitch) {
        if (service::audio::isInputMuted()) lv_obj_add_state(ctx->inputMuteSwitch, LV_STATE_CHECKED);
        else lv_obj_remove_state(ctx->inputMuteSwitch, LV_STATE_CHECKED);
    }
    if (ctx->inputVolumeSlider) {
        lvgl_sliderbox_set_value(ctx->inputVolumeSlider, static_cast<int32_t>(service::audio::getInputVolume()), LV_ANIM_OFF);
    }

    if (ctx->outputEnabledSwitch) {
        if (service::audio::isOutputEnabled()) lv_obj_add_state(ctx->outputEnabledSwitch, LV_STATE_CHECKED);
        else lv_obj_remove_state(ctx->outputEnabledSwitch, LV_STATE_CHECKED);
    }
    if (ctx->outputMuteSwitch) {
        if (service::audio::isOutputMuted()) lv_obj_add_state(ctx->outputMuteSwitch, LV_STATE_CHECKED);
        else lv_obj_remove_state(ctx->outputMuteSwitch, LV_STATE_CHECKED);
    }
    if (ctx->outputVolumeSlider) {
        lvgl_sliderbox_set_value(ctx->outputVolumeSlider, static_cast<int32_t>(service::audio::getOutputVolume()), LV_ANIM_OFF);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Audio");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    if (!service::audio::isAvailable()) {
        auto* label = lv_label_create(main_wrapper);
        lv_label_set_text(label, "No audio hardware available");
        lv_obj_center(label);
        return;
    }

    // Gated per-direction, not just isAvailable() - a mic-only or speaker-only
    // device (e.g. a dedicated input codec with no output codec bound) should
    // only show the section it actually has, not a dead section for the other.
    if (service::audio::isInputAvailable()) {
        auto* input_section = createSection(main_wrapper, "Microphone");
        ctx->inputEnabledSwitch = createSwitchRow(input_section, "Enabled", onInputEnabledSwitch, ctx);
        ctx->inputMuteSwitch = createSwitchRow(input_section, "Mute", onInputMuteSwitch, ctx);
        ctx->inputVolumeSlider = createSliderRow(input_section, "Volume", static_cast<int32_t>(service::audio::getInputVolume()), onInputVolumeSlider, ctx);
    }

    if (service::audio::isOutputAvailable()) {
        auto* output_section = createSection(main_wrapper, "Speaker");
        ctx->outputEnabledSwitch = createSwitchRow(output_section, "Enabled", onOutputEnabledSwitch, ctx);
        ctx->outputMuteSwitch = createSwitchRow(output_section, "Mute", onOutputMuteSwitch, ctx);
        ctx->outputVolumeSlider = createSliderRow(output_section, "Volume", static_cast<int32_t>(service::audio::getOutputVolume()), onOutputVolumeSlider, ctx);
    }

    // isAvailable() only reflects that the audio-stream device exists, not that any
    // codec is actually bound to it (the stream device is constructed unconditionally
    // at module-start time, before devicetree codecs exist, and binds lazily on first
    // use) -- so a board with no input or output codec at all reaches here with both
    // sections skipped above and would otherwise show an empty page.
    if (!service::audio::isInputAvailable() && !service::audio::isOutputAvailable()) {
        auto* label = lv_label_create(main_wrapper);
        lv_label_set_text(label, "No supported audio controls");
        lv_obj_center(label);
    }

    refresh(ctx);

    ctx->audioSubscription = service::audio::getPubsub()->subscribe([ctx](auto) {
        lvgl_lock();
        refresh(ctx);
        lvgl_unlock();
    });
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            default:
                break;
        }
    }

    if (ctx.audioSubscription != nullptr) {
        service::audio::getPubsub()->unsubscribe(ctx.audioSubscription);
        ctx.audioSubscription = nullptr;
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "AudioSettings",
    .name = "Audio",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::audiosettings
