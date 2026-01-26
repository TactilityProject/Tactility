#include <Tactility/app/selectiondialog/SelectionDialog.h>

#include <Tactility/Logger.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/StringUtils.h>

#include <lvgl.h>

namespace tt::app::selectiondialog {

constexpr auto* PARAMETER_BUNDLE_KEY_TITLE = "title";
constexpr auto* PARAMETER_BUNDLE_KEY_ITEMS = "items";
constexpr auto* RESULT_BUNDLE_KEY_INDEX = "index";

constexpr auto* PARAMETER_ITEM_CONCATENATION_TOKEN = ";;";
constexpr auto* DEFAULT_TITLE = "Select...";

static const auto LOGGER = Logger("SelectionDialog");

extern const AppManifest manifest;

LaunchId start(const std::string& title, const std::vector<std::string>& items) {
    std::string items_joined = string::join(items, PARAMETER_ITEM_CONCATENATION_TOKEN);
    auto bundle = std::make_shared<Bundle>();
    bundle->putString(PARAMETER_BUNDLE_KEY_TITLE, title);
    bundle->putString(PARAMETER_BUNDLE_KEY_ITEMS, items_joined);
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

class SelectionDialogApp final : public App {

    static void onListItemSelectedCallback(lv_event_t* e) {
        auto app = std::static_pointer_cast<SelectionDialogApp>(getCurrentApp());
        assert(app != nullptr);
        app->onListItemSelected(e);
    }

    void onListItemSelected(lv_event_t* e) {
        auto index = reinterpret_cast<std::size_t>(lv_event_get_user_data(e));
        LOGGER.info("Selected item at index {}", index);
        auto bundle = std::make_unique<Bundle>();
        bundle->putInt32(RESULT_BUNDLE_KEY_INDEX, (int32_t)index);
        setResult(Result::Ok, std::move(bundle));
        stop(manifest.appId);
    }

    static void createChoiceItem(void* parent, const std::string& title, size_t index) {
        auto* list = static_cast<lv_obj_t*>(parent);
        lv_obj_t* btn = lv_list_add_button(list, nullptr, title.c_str());
        lv_obj_add_event_cb(btn, onListItemSelectedCallback, LV_EVENT_SHORT_CLICKED, (void*)index);
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        std::string title = getTitleParameter(app.getParameters());
        lvgl::toolbar_create(parent, title);

        auto* list = lv_list_create(parent);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_flex_grow(list, 1);

        auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");
        std::string items_concatenated;
        if (parameters->optString(PARAMETER_BUNDLE_KEY_ITEMS, items_concatenated)) {
            std::vector<std::string> items = string::split(items_concatenated, PARAMETER_ITEM_CONCATENATION_TOKEN);
            if (items.empty() || items.front().empty()) {
                LOGGER.error("No items provided");
                setResult(Result::Error);
                stop(manifest.appId);
            } else if (items.size() == 1) {
                auto result_bundle = std::make_unique<Bundle>();
                result_bundle->putInt32(RESULT_BUNDLE_KEY_INDEX, 0);
                setResult(Result::Ok, std::move(result_bundle));
                stop(manifest.appId);
                LOGGER.warn("Auto-selecting single item");
            } else {
                size_t index = 0;
                for (const auto& item: items) {
                    createChoiceItem(list, item, index++);
                }
            }
        } else {
            LOGGER.error("No items provided");
            setResult(Result::Error);
            stop(manifest.appId);
        }
    }
};

extern const AppManifest manifest = {
    .appId = "SelectionDialog",
    .appName = "Selection Dialog",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<SelectionDialogApp>
};

}
