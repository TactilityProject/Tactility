#include <Tactility/Tactility.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/settings/AudioSettings.h>
#include <Tactility/hal/audio/AudioDevice.h>
#include <Tactility/Logger.h>

#include <lvgl.h>
#include <tactility/lvgl_icon_shared.h>
#include <tactility/lvgl_module.h>

namespace tt::app::audio {

static const auto LOGGER = Logger("Audio");

class AudioApp final : public App {

    settings::audio::AudioSettings audioSettings;
    bool audioSettingsUpdated = false;

    static void applyVolumeToAllDevices(uint8_t volume, bool muted) {
        auto devices = hal::findDevices<hal::audio::AudioDevice>(hal::Device::Type::Audio);
        for (const auto& device : devices) {
            device->setVolume(volume);
            device->setMuted(muted);
        }
    }

    static void onVolumeSliderEvent(lv_event_t* event) {
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
        auto* app = static_cast<AudioApp*>(lv_event_get_user_data(event));

        int32_t slider_value = lv_slider_get_value(slider);
        app->audioSettings.volume = static_cast<uint8_t>(slider_value);
        app->audioSettingsUpdated = true;

        applyVolumeToAllDevices(app->audioSettings.volume, app->audioSettings.muted);
    }

    static void onMuteSwitch(lv_event_t* event) {
        auto* app = static_cast<AudioApp*>(lv_event_get_user_data(event));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
        bool muted = lv_obj_has_state(sw, LV_STATE_CHECKED);
        app->audioSettings.muted = muted;
        app->audioSettingsUpdated = true;

        applyVolumeToAllDevices(app->audioSettings.volume, app->audioSettings.muted);
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        audioSettings = settings::audio::loadOrGetDefault();
        auto ui_density = lvgl_get_ui_density();

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);

        // Volume slider
        auto* volume_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(volume_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(volume_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(volume_wrapper, 0, LV_STATE_DEFAULT);
        if (ui_density != LVGL_UI_DENSITY_COMPACT) {
            lv_obj_set_style_pad_ver(volume_wrapper, 4, LV_STATE_DEFAULT);
        }

        auto* volume_label = lv_label_create(volume_wrapper);
        lv_label_set_text(volume_label, "Volume");
        lv_obj_align(volume_label, LV_ALIGN_LEFT_MID, 0, 0);

        auto* volume_slider = lv_slider_create(volume_wrapper);
        lv_obj_set_width(volume_slider, LV_PCT(50));
        lv_obj_align(volume_slider, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_slider_set_range(volume_slider, 0, 100);
        lv_obj_add_event_cb(volume_slider, onVolumeSliderEvent, LV_EVENT_VALUE_CHANGED, this);
        lv_slider_set_value(volume_slider, audioSettings.volume, LV_ANIM_OFF);

        // Mute switch
        auto* mute_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(mute_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(mute_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(mute_wrapper, 0, LV_STATE_DEFAULT);

        auto* mute_label = lv_label_create(mute_wrapper);
        lv_label_set_text(mute_label, "Mute");
        lv_obj_align(mute_label, LV_ALIGN_LEFT_MID, 0, 0);

        auto* mute_switch = lv_switch_create(mute_wrapper);
        if (audioSettings.muted) {
            lv_obj_add_state(mute_switch, LV_STATE_CHECKED);
        }
        lv_obj_align(mute_switch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(mute_switch, onMuteSwitch, LV_EVENT_VALUE_CHANGED, this);
    }

    void onHide(AppContext& app) override {
        if (audioSettingsUpdated) {
            const settings::audio::AudioSettings settings_to_save = audioSettings;
            getMainDispatcher().dispatch([settings_to_save] {
                settings::audio::save(settings_to_save);
            });
        }
    }
};

extern const AppManifest manifest = {
    .appId = "Audio",
    .appName = "Audio",
    .appIcon = LVGL_ICON_SHARED_MUSIC_NOTE,
    .appCategory = Category::Settings,
    .createApp = create<AudioApp>
};

} // namespace
