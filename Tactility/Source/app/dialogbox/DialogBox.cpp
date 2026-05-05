#include "Tactility/app/dialogbox/DialogBox.h"

#include <Tactility/app/AppManifest.h>
#include <Tactility/Logger.h>
#include <Tactility/Timer.h>

#include <lvgl.h>

#include <memory>
#include <tactility/check.h>

namespace tt::app::dialogbox {

#define PARAMETER_BUNDLE_KEY_TITLE "title"
#define PARAMETER_BUNDLE_KEY_MESSAGE "message"
#define PARAMETER_BUNDLE_KEY_TIMEOUT "timeoutMs"

static const auto LOGGER = Logger("DialogBox");

extern const AppManifest manifest;

LaunchId start(const std::string& title, const std::string& message, uint32_t timeoutMs) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_TITLE, title);
    bundle->putString(PARAMETER_BUNDLE_KEY_MESSAGE, message);
    bundle->putInt32(PARAMETER_BUNDLE_KEY_TIMEOUT, (int32_t)timeoutMs);
    return app::start(manifest.appId, bundle);
}

LaunchId start(const std::string& title, const std::string& message) {
    return start(title, message, 0);
}

LaunchId start(const std::string& message, uint32_t timeoutMs) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_MESSAGE, message);
    bundle->putInt32(PARAMETER_BUNDLE_KEY_TIMEOUT, (int32_t)timeoutMs);
    return app::start(manifest.appId, bundle);
}

LaunchId start(const std::string& message) {
    return start(message, 0);
}

class DialogBoxApp : public App {
    lv_obj_t* overlay = nullptr;
    std::unique_ptr<Timer> autoDismissTimer;

    static void onOverlayClickedCallback(lv_event_t* e) {
        auto app = std::static_pointer_cast<DialogBoxApp>(getCurrentApp());
        assert(app != nullptr);
        app->onOverlayClicked(e);
    }

    void onOverlayClicked(lv_event_t* e) {
        LOGGER.info("Touch dismissed dialog");
        setResult(Result::Ok);
        stop(manifest.appId);
    }

    static void onAutoDismiss() {
        auto appContext = getCurrentAppContext();
        if (appContext != nullptr && appContext->getManifest().appId == manifest.appId) {
            auto app = std::static_pointer_cast<DialogBoxApp>(appContext->getApp());
            app->dismiss();
        }
    }

    void dismiss() {
        LOGGER.info("Auto-dismiss dialog");
        setResult(Result::Ok);
        stop(manifest.appId);
    }

public:
    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");

        std::string message;
        parameters->optString(PARAMETER_BUNDLE_KEY_MESSAGE, message);

        std::string title;
        bool hasTitle = parameters->optString(PARAMETER_BUNDLE_KEY_TITLE, title);

        lv_obj_t* top = lv_layer_top();

        lv_coord_t screenW = lv_display_get_horizontal_resolution(nullptr);

        overlay = lv_obj_create(top);
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, onOverlayClickedCallback, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* box = lv_obj_create(overlay);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_radius(box, 12, 0);
        lv_obj_set_style_pad_all(box, 16, 0);
        lv_obj_set_style_pad_gap(box, 8, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_width(box, std::min(screenW - 40, lv_coord_t(280)));

        if (hasTitle && !title.empty()) {
            lv_obj_t* title_label = lv_label_create(box);
            lv_label_set_text(title_label, title.c_str());
            lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
        }

        lv_obj_t* message_label = lv_label_create(box);
        lv_label_set_text(message_label, message.c_str());
        lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(message_label, lv_color_hex(0xEBEBF5), 0);
        lv_obj_set_width(message_label, LV_PCT(100));

        int32_t timeoutMs = 0;
        parameters->optInt32(PARAMETER_BUNDLE_KEY_TIMEOUT, timeoutMs);
        if (timeoutMs > 0) {
            autoDismissTimer = std::make_unique<Timer>(
                Timer::Type::Once,
                kernel::millisToTicks((uint32_t)timeoutMs),
                onAutoDismiss
            );
            autoDismissTimer->start();
        }
    }

    void onHide(AppContext& app) override {
        autoDismissTimer.reset();
        if (overlay != nullptr) {
            lv_obj_delete(overlay);
            overlay = nullptr;
        }
    }
};

extern const AppManifest manifest = {
    .appId = "DialogBox",
    .appName = "Dialog Box",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<DialogBoxApp>
};

}
