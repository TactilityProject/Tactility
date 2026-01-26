#include "Tactility/app/alertdialog/AlertDialog.h"

#include "Tactility/lvgl/Toolbar.h"
#include "Tactility/service/loader/Loader.h"

#include <Tactility/Logger.h>
#include <Tactility/StringUtils.h>

#include <lvgl.h>

namespace tt::app::alertdialog {

#define PARAMETER_BUNDLE_KEY_TITLE "title"
#define PARAMETER_BUNDLE_KEY_MESSAGE "message"
#define PARAMETER_BUNDLE_KEY_BUTTON_LABELS "buttonLabels"
#define RESULT_BUNDLE_KEY_INDEX "index"

#define PARAMETER_ITEM_CONCATENATION_TOKEN ";;"
#define DEFAULT_TITLE ""

static const auto LOGGER = Logger("AlertDialog");

extern const AppManifest manifest;

LaunchId start(const std::string& title, const std::string& message, const std::vector<std::string>& buttonLabels) {
    std::string items_joined = string::join(buttonLabels, PARAMETER_ITEM_CONCATENATION_TOKEN);
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_TITLE, title);
    bundle->putString(PARAMETER_BUNDLE_KEY_MESSAGE, message);
    bundle->putString(PARAMETER_BUNDLE_KEY_BUTTON_LABELS, items_joined);
    return app::start(manifest.appId, bundle);
}

LaunchId start(const std::string& title, const std::string& message, const std::vector<const char*>& buttonLabels) {
    std::string items_joined = string::join(buttonLabels, PARAMETER_ITEM_CONCATENATION_TOKEN);
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_TITLE, title);
    bundle->putString(PARAMETER_BUNDLE_KEY_MESSAGE, message);
    bundle->putString(PARAMETER_BUNDLE_KEY_BUTTON_LABELS, items_joined);
    return app::start(manifest.appId, bundle);
}

LaunchId start(const std::string& title, const std::string& message) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_TITLE, title);
    bundle->putString(PARAMETER_BUNDLE_KEY_MESSAGE, message);
    bundle->putString(PARAMETER_BUNDLE_KEY_BUTTON_LABELS, "OK");
    return app::start(manifest.appId, bundle);
}

int32_t getResultIndex(const Bundle& bundle) {
    int32_t index = -1;
    bundle.optInt32(RESULT_BUNDLE_KEY_INDEX, index);
    return index;
}

static std::string getTitleParameter(std::shared_ptr<const Bundle> bundle) {
    std::string result;
    if (bundle->optString(PARAMETER_BUNDLE_KEY_TITLE, result)) {
        return result;
    } else {
        return DEFAULT_TITLE;
    }
}


class AlertDialogApp : public App {

    static void onButtonClickedCallback(lv_event_t* e) {
        auto app = std::static_pointer_cast<AlertDialogApp>(getCurrentApp());
        assert(app != nullptr);
        app->onButtonClicked(e);
    }

    void onButtonClicked(lv_event_t* e) {
        auto index = reinterpret_cast<std::size_t>(lv_event_get_user_data(e));
        LOGGER.info("Selected item at index {}", index);

        auto bundle = std::make_unique<Bundle>();
        bundle->putInt32(RESULT_BUNDLE_KEY_INDEX, (int32_t)index);
        setResult(Result::Ok, std::move(bundle));

        stop(manifest.appId);
    }

    static void createButton(lv_obj_t* parent, const std::string& text, size_t index) {
        lv_obj_t* button = lv_button_create(parent);
        lv_obj_t* button_label = lv_label_create(button);
        lv_obj_align(button_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(button_label, text.c_str());
        lv_obj_add_event_cb(button, onButtonClickedCallback, LV_EVENT_SHORT_CLICKED, (void*)index);
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");

        std::string title = getTitleParameter(app.getParameters());
        lv_obj_t* toolbar = lvgl::toolbar_create(parent, title);
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* message_label = lv_label_create(parent);
        lv_obj_align(message_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_width(message_label, LV_PCT(80));
        lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);

        std::string message;
        if (parameters->optString(PARAMETER_BUNDLE_KEY_MESSAGE, message)) {
            lv_label_set_text(message_label, message.c_str());
            lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
        }

        lv_obj_t* button_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(button_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(button_wrapper, 0, 0);
        lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(button_wrapper, 0, 0);
        lv_obj_align(button_wrapper, LV_ALIGN_BOTTOM_MID, 0, -4);

        std::string items_concatenated;
        if (parameters->optString(PARAMETER_BUNDLE_KEY_BUTTON_LABELS, items_concatenated)) {
            std::vector<std::string> labels = string::split(items_concatenated, PARAMETER_ITEM_CONCATENATION_TOKEN);
            size_t index = 0;
            for (const auto& label: labels) {
                createButton(button_wrapper, label, index++);
            }
        }
    }
};

extern const AppManifest manifest = {
    .appId = "AlertDialog",
    .appName = "Alert Dialog",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<AlertDialogApp>
};

}
