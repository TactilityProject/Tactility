#include <Tactility/app/inputdialog/InputDialog.h>

#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/TactilityCore.h>

#include <lvgl.h>

namespace tt::app::inputdialog {

constexpr auto* PARAMETER_BUNDLE_KEY_TITLE = "title";
constexpr auto* PARAMETER_BUNDLE_KEY_MESSAGE = "message";
constexpr auto* PARAMETER_BUNDLE_KEY_PREFILLED = "prefilled";
constexpr auto* RESULT_BUNDLE_KEY_RESULT = "result";

constexpr auto* DEFAULT_TITLE = "Input";

static const auto LOGGER = Logger("InputDialog");

extern const AppManifest manifest;
class InputDialogApp;

LaunchId start(const std::string& title, const std::string& message, const std::string& prefilled) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_TITLE, title);
    bundle->putString(PARAMETER_BUNDLE_KEY_MESSAGE, message);
    bundle->putString(PARAMETER_BUNDLE_KEY_PREFILLED, prefilled);
    return app::start(manifest.appId, bundle);
}

std::string getResult(const Bundle& bundle) {
    std::string result;
    bundle.optString(RESULT_BUNDLE_KEY_RESULT, result);
    return result;
}

static std::string getTitleParameter(const std::shared_ptr<const Bundle>& bundle) {
    std::string result;
    if (bundle->optString(PARAMETER_BUNDLE_KEY_TITLE, result)) {
        return result;
    } else {
        return DEFAULT_TITLE;
    }
}

class InputDialogApp final : public App {

    static void createButton(lv_obj_t* parent, const std::string& text, void* callbackContext) {
        lv_obj_t* button = lv_button_create(parent);
        lv_obj_t* button_label = lv_label_create(button);
        lv_obj_align(button_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(button_label, text.c_str());
        lv_obj_add_event_cb(button, onButtonClickedCallback, LV_EVENT_SHORT_CLICKED, callbackContext);
    }

    static void onButtonClickedCallback(lv_event_t* e) {
        auto app = std::static_pointer_cast<InputDialogApp>(getCurrentApp());
        assert(app != nullptr);
        app->onButtonClicked(e);
    }

    void onButtonClicked(lv_event_t* e) {
        auto user_data = lv_event_get_user_data(e);
        int index = (user_data != 0) ? 0 : 1;
        LOGGER.info("Selected item at index {}", index);
        if (index == 0) {
            auto bundle = std::make_unique<Bundle>();
            const char* text = lv_textarea_get_text((lv_obj_t*)user_data);
            bundle->putString(RESULT_BUNDLE_KEY_RESULT, text);
            setResult(Result::Ok, std::move(bundle));
        } else {
            setResult(Result::Cancelled);

        }
        stop(manifest.appId);
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");

        std::string title = getTitleParameter(app.getParameters());
        auto* toolbar = lvgl::toolbar_create(parent, title);
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        auto* message_label = lv_label_create(parent);
        lv_obj_align(message_label, LV_ALIGN_CENTER, 0, -20);
        lv_obj_set_width(message_label, LV_PCT(80));

        std::string message;
        if (parameters->optString(PARAMETER_BUNDLE_KEY_MESSAGE, message)) {
            lv_label_set_text(message_label, message.c_str());
            lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
        }

        auto* textarea = lv_textarea_create(parent);
        lv_obj_align_to(textarea, message_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        lv_textarea_set_one_line(textarea, true);
        std::string prefilled;
        if (parameters->optString(PARAMETER_BUNDLE_KEY_PREFILLED, prefilled)) {
            lv_textarea_set_text(textarea, prefilled.c_str());
        }

        auto* button_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(button_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(button_wrapper, 0, 0);
        lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(button_wrapper, 0, 0);
        lv_obj_align(button_wrapper, LV_ALIGN_BOTTOM_MID, 0, -4);

        createButton(button_wrapper, "OK", textarea);
        createButton(button_wrapper, "Cancel", nullptr);
    }
};

extern const AppManifest manifest = {
    .appId = "InputDialog",
    .appName = "Input Dialog",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<InputDialogApp>
};

}
