#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_module.h>

#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/setup/Setup.h>

#include <Tactility/Preferences.h>
#include <Tactility/StringUtils.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/app/wifimanage/WifiManage.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/wifi/Wifi.h>

#include <lvgl.h>

#include <functional>
#include <vector>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
#include <Tactility/app/touchcalibration/TouchCalibration.h>
#endif

namespace tt::app::setup {

extern const AppManifest manifest;

constexpr auto* PREFERENCES_NAMESPACE = "setup";
constexpr auto* PREFERENCES_KEY_COMPLETED = "completed";

bool isCompleted() {
    Preferences preferences(PREFERENCES_NAMESPACE);
    bool completed = false;
    preferences.optBool(PREFERENCES_KEY_COMPLETED, completed);
    return completed;
}

static void markCompleted() {
    Preferences preferences(PREFERENCES_NAMESPACE);
    preferences.putBool(PREFERENCES_KEY_COMPLETED, true);
}

struct StepConfiguration {
    std::string title;
    std::string description;
    std::function<void()> run;
};

class SetupApp final : public App {

    enum class Phase {
        Welcome,
        StepIntro,
        Done
    };

    Phase phase = Phase::Welcome;
    size_t stepIndex = 0;
    std::vector<StepConfiguration> steps;
    bool isShown = false;

    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* descriptionLabel = nullptr;
    lv_obj_t* skipButton = nullptr;
    lv_obj_t* continueButton = nullptr;

    static void onSkipClickedCallback(lv_event_t* e) {
        auto* app = (SetupApp*)lv_event_get_user_data(e);
        app->onSkipClicked();
    }

    static void onContinueClickedCallback(lv_event_t* e) {
        auto* app = (SetupApp*)lv_event_get_user_data(e);
        app->onContinueClicked();
    }

    void renderCurrent() {
        switch (phase) {
            case Phase::Welcome: {
                lv_label_set_text(titleLabel, "Welcome");
                auto device_names = string::split(std::string(CONFIG_TT_DEVICE_NAME_SIMPLE), ",");
                lv_label_set_text_fmt(descriptionLabel, "It's time to set up your %s!", device_names.front().c_str());
                lv_obj_add_flag(skipButton, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(continueButton, 0), "Continue");
                break;
            }
            case Phase::StepIntro: {
                const auto& step = steps[stepIndex];
                lv_label_set_text(titleLabel, step.title.c_str());
                lv_label_set_text(descriptionLabel, step.description.c_str());
                lv_obj_remove_flag(skipButton, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(skipButton, 0), "Skip");
                lv_label_set_text(lv_obj_get_child(continueButton, 0), "Continue");
                break;
            }
            case Phase::Done:
                lv_label_set_text(titleLabel, "Setup Complete");
                lv_label_set_text(descriptionLabel, "You're all set.");
                lv_obj_add_flag(skipButton, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(continueButton, 0), "Finish");
                break;
        }
    }

    void advanceTo(size_t index) {
        if (index < steps.size()) {
            stepIndex = index;
            phase = Phase::StepIntro;
        } else {
            phase = Phase::Done;
        }

        // Widgets may not exist yet: onShow() runs asynchronously on the GUI task and
        // may not have (re)created them by the time onResult() advances the state.
        // onShow() calls renderCurrent() itself once the widgets are ready.
        if (isShown) {
            renderCurrent();
        }
    }

    void onSkipClicked() {
        if (phase == Phase::StepIntro) {
            advanceTo(stepIndex + 1);
        }
    }

    void onContinueClicked() {
        switch (phase) {
            case Phase::Welcome:
                advanceTo(0);
                break;
            case Phase::StepIntro:
                steps[stepIndex].run();
                break;
            case Phase::Done:
                markCompleted();
                stop(manifest.appId);
                break;
        }
    }

public:

    void onCreate(AppContext& app) override {
        steps = {
#if defined(CONFIG_TT_TOUCH_CALIBRATION_REQUIRED)
            {
                .title = "Touch Calibration",
                .description = "Let's calibrate the touch screen.",
                .run = [] { touchcalibration::start(); }
            },
#endif
            {
                .title = "Time Zone Setup",
                .description = "Let's set the time zone.",
                .run = [] { timezone::start(true); }
            },
            {
                .title = "Wi-Fi Setup",
                .description = "Let's connect to a Wi-Fi access point.",
                .run = [] {
                    service::wifi::setEnabled(true);
                    wifimanage::start();
                }
            }
        };
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        titleLabel = lv_label_create(parent);
        lv_obj_set_width(titleLabel, LV_PCT(80));
        lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_WRAP);
        auto* font = lvgl_get_text_font(FONT_SIZE_LARGE);
        lv_obj_set_style_text_font(titleLabel, font, 0);

        descriptionLabel = lv_label_create(parent);
        lv_obj_set_width(descriptionLabel, LV_PCT(80));
        lv_obj_set_style_text_align(descriptionLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(descriptionLabel, LV_LABEL_LONG_WRAP);
        lv_obj_align(descriptionLabel, LV_ALIGN_CENTER, 0, 0);

        int title_margin = lvgl_get_text_font_height(FONT_SIZE_LARGE);
        lv_obj_align_to(titleLabel, descriptionLabel, LV_ALIGN_OUT_TOP_MID, 0, -title_margin);

        skipButton = lv_button_create(parent);
        lv_obj_t* skip_label = lv_label_create(skipButton);
        lv_label_set_text(skip_label, "Skip");
        lv_obj_center(skip_label);
        lv_obj_align(skipButton, LV_ALIGN_BOTTOM_LEFT, 12, -12);
        lv_obj_add_event_cb(skipButton, onSkipClickedCallback, LV_EVENT_SHORT_CLICKED, this);

        continueButton = lv_button_create(parent);
        lv_obj_t* continue_label = lv_label_create(continueButton);
        lv_label_set_text(continue_label, "Continue");
        lv_obj_center(continue_label);
        lv_obj_align(continueButton, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
        lv_obj_add_event_cb(continueButton, onContinueClickedCallback, LV_EVENT_SHORT_CLICKED, this);

        isShown = true;
        renderCurrent();
    }

    void onHide(AppContext& app) override {
        isShown = false;
    }

    void onResult(AppContext& app, LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) override {
        lvgl_lock();
        advanceTo(stepIndex + 1);
        lvgl_unlock();
    }
};

extern const AppManifest manifest = {
    .appId = "Setup",
    .appName = "Setup",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden | AppManifest::Flags::HideStatusBar,
    .createApp = create<SetupApp>
};

LaunchId start() {
    return app::start(manifest.appId);
}

}
