#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/AppPaths.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/MountPoints.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/StringUtils.h>
#include <Tactility/Timer.h>

#include <lvgl.h>
#include <memory>

namespace tt::app::timezone {

static const auto LOGGER = Logger("TimeZone");

constexpr auto* RESULT_BUNDLE_CODE_INDEX = "code";
constexpr auto* RESULT_BUNDLE_NAME_INDEX = "name";

extern const AppManifest manifest;

struct TimeZoneEntry {
    std::string name;
    std::string code;
};

static bool parseEntry(const std::string& input, std::string& outName, std::string& outCode) {
    std::string partial_strip = input.substr(1, input.size() - 3);
    auto first_end_quote = partial_strip.find('"');
    if (first_end_quote == std::string::npos) {
        return false;
    } else {
        outName = partial_strip.substr(0, first_end_quote);
        outCode = partial_strip.substr(first_end_quote + 3);
        return true;
    }
}

// region Result

std::string getResultName(const Bundle& bundle) {
    std::string result;
    bundle.optString(RESULT_BUNDLE_NAME_INDEX, result);
    return result;
}

std::string getResultCode(const Bundle& bundle) {
    std::string result;
    bundle.optString(RESULT_BUNDLE_CODE_INDEX, result);
    return result;
}

void setResultName(Bundle& bundle, const std::string& name) {
    bundle.putString(RESULT_BUNDLE_NAME_INDEX, name);
}

void setResultCode(Bundle& bundle, const std::string& code) {
    bundle.putString(RESULT_BUNDLE_CODE_INDEX, code);
}

// endregion

class TimeZoneApp final : public App {

    Mutex mutex;
    std::vector<TimeZoneEntry> entries;
    std::unique_ptr<Timer> updateTimer;
    lv_obj_t* listWidget = nullptr;
    lv_obj_t* filterTextareaWidget = nullptr;

    static void onTextareaValueChangedCallback(lv_event_t* e) {
        auto* app = (TimeZoneApp*)lv_event_get_user_data(e);
        app->onTextareaValueChanged(e);
    }

    void onTextareaValueChanged(lv_event_t* e) {
        if (mutex.lock(100 / portTICK_PERIOD_MS)) {
            if (updateTimer->isRunning()) {
                updateTimer->stop();
            }

            updateTimer->start();

            mutex.unlock();
        }
    }

    static void onListItemSelectedCallback(lv_event_t* e) {
        auto index = reinterpret_cast<std::size_t>(lv_event_get_user_data(e));
        auto app = std::static_pointer_cast<TimeZoneApp>(getCurrentApp());
        assert(app != nullptr);
        app->onListItemSelected(index);
    }

    void onListItemSelected(std::size_t index) {
        LOGGER.info("Selected item at index {}", index);

        auto& entry = entries[index];

        auto bundle = std::make_unique<Bundle>();
        setResultName(*bundle, entry.name);
        setResultCode(*bundle, entry.code);

        setResult(Result::Ok, std::move(bundle));
        stop(manifest.appId);
    }

    static void createListItem(lv_obj_t* list, const std::string& title, size_t index) {
        auto* btn = lv_list_add_button(list, nullptr, title.c_str());
        lv_obj_add_event_cb(btn, &onListItemSelectedCallback, LV_EVENT_SHORT_CLICKED, (void*)index);
    }

    void readTimeZones(std::string filter) {
        auto path = std::string(file::MOUNT_POINT_SYSTEM) + "/timezones.csv";
        auto* file = fopen(path.c_str(), "rb");
        if (file == nullptr) {
            LOGGER.error("Failed to open {}", path);
            return;
        }
        char line[96];
        std::string name;
        std::string code;
        uint32_t count = 0;
        std::vector<TimeZoneEntry> new_entries;
        while (fgets(line, 96, file)) {
            if (parseEntry(line, name, code)) {
                if (string::lowercase(name).find(filter) != std::string::npos) {
                    count++;
                    new_entries.push_back({.name = name, .code = code});

                    // Safety guard
                    if (count > 50) {
                        // TODO: Show warning that we're not displaying a complete list
                        break;
                    }
                }
            } else {
                LOGGER.error("Parse error at line {}", count);
            }
        }

        fclose(file);

        if (mutex.lock(100 / portTICK_PERIOD_MS)) {
            entries = std::move(new_entries);
            mutex.unlock();
        } else {
            LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        }

        LOGGER.info("Processed {} entries", count);
    }

    void updateList() {
        if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
            std::string filter = string::lowercase(std::string(lv_textarea_get_text(filterTextareaWidget)));
            readTimeZones(filter);
            lvgl::unlock();
        } else {
            LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
            return;
        }

        if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
            if (mutex.lock(100 / portTICK_PERIOD_MS)) {
                lv_obj_clean(listWidget);

                uint32_t index = 0;
                for (auto& entry : entries) {
                    createListItem(listWidget, entry.name, index);
                    index++;
                }

                mutex.unlock();
            }

            lvgl::unlock();
        }
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        auto* search_wrapper = lv_obj_create(parent);
        lv_obj_set_size(search_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(search_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(search_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(search_wrapper, 0, 0);
        lv_obj_set_style_border_width(search_wrapper, 0, 0);

        auto* icon = lv_image_create(search_wrapper);
        lv_obj_set_style_margin_left(icon, 8, 0);
        lv_obj_set_style_image_recolor_opa(icon, 255, 0);
        lv_obj_set_style_image_recolor(icon, lv_theme_get_color_primary(parent), 0);

        std::string icon_path = lvgl::PATH_PREFIX + app.getPaths()->getAssetsPath("search.png");
        lv_image_set_src(icon, icon_path.c_str());
        lv_obj_set_style_image_recolor(icon, lv_theme_get_color_primary(parent), 0);

        auto* textarea = lv_textarea_create(search_wrapper);
        lv_textarea_set_placeholder_text(textarea, "e.g. Europe/Amsterdam");
        lv_textarea_set_one_line(textarea, true);
        lv_obj_add_event_cb(textarea, onTextareaValueChangedCallback, LV_EVENT_VALUE_CHANGED, this);
        filterTextareaWidget = textarea;
        lv_obj_set_flex_grow(textarea, 1);

        auto* list = lv_list_create(parent);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_flex_grow(list, 1);
        lv_obj_set_style_border_width(list, 0, 0);
        listWidget = list;
    }

    void onCreate(AppContext& app) override {
        updateTimer = std::make_unique<Timer>(Timer::Type::Once, 500 / portTICK_PERIOD_MS, [this] {
            updateList();
        });
    }
};

extern const AppManifest manifest = {
    .appId = "TimeZone",
    .appName = "Select timezone",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<TimeZoneApp>
};

LaunchId start() {
    return app::start(manifest.appId);
}

}
