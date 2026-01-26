#include <Tactility/Assets.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/app/localesettings/TextResources.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/settings/Time.h>
#include <Tactility/StringUtils.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <lvgl.h>
#include <map>
#include <sstream>

namespace tt::app::localesettings {

constexpr auto* TAG = "LocaleSettings";

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/LocaleSettings/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/LocaleSettings/i18n";
#endif

extern const AppManifest manifest;

class LocaleSettingsApp final : public App {
    tt::i18n::TextResources textResources = tt::i18n::TextResources(TEXT_RESOURCE_PATH);
    RecursiveMutex mutex;
    lv_obj_t* regionTextArea = nullptr;
    lv_obj_t* languageDropdown = nullptr;
    bool settingsUpdated = false;

    std::map<settings::Language, std::string> languageMap;

    std::string getLanguageOptions() const {
        std::vector<std::string> items;
        for (int i = 0; i < static_cast<int>(settings::Language::count); i++) {
            switch (static_cast<settings::Language>(i)) {
                case settings::Language::en_GB:
                    items.push_back(textResources[i18n::Text::EN_GB]);
                    break;
                case settings::Language::en_US:
                    items.push_back(textResources[i18n::Text::EN_US]);
                    break;
                case settings::Language::fr_FR:
                    items.push_back(textResources[i18n::Text::FR_FR]);
                    break;
                case settings::Language::nl_BE:
                    items.push_back(textResources[i18n::Text::NL_BE]);
                    break;
                case settings::Language::nl_NL:
                    items.push_back(textResources[i18n::Text::NL_NL]);
                    break;
                case settings::Language::count:
                    break;
            }
        }
        return string::join(items, "\n");
    }

    void updateViews() {
        textResources.load();

        std::string language_options = getLanguageOptions();
        lv_dropdown_set_options(languageDropdown, language_options.c_str());
        lv_dropdown_set_selected(languageDropdown, static_cast<uint32_t>(settings::getLanguage()));
    }

    static void onLanguageSet(lv_event_t* event) {
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
        auto index = lv_dropdown_get_selected(dropdown);
        auto language = static_cast<settings::Language>(index);
        settings::setLanguage(language);

        auto* self = static_cast<LocaleSettingsApp*>(lv_event_get_user_data(event));
        self->updateViews();
    }

    static void onRegionChanged(lv_event_t* event) {
        auto* self = static_cast<LocaleSettingsApp*>(lv_event_get_user_data(event));
        self->settingsUpdated = true;
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto ui_scale = hal::getConfiguration()->uiScale;

        textResources.load();

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);

        // Region

        auto* region_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_width(region_wrapper, LV_PCT(100));
        lv_obj_set_height(region_wrapper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(region_wrapper, 8, 0);
        lv_obj_set_style_border_width(region_wrapper, 0, 0);

        auto* region_label = lv_label_create(region_wrapper);
        lv_label_set_text(region_label, textResources[i18n::Text::REGION].c_str());
        lv_obj_align(region_label, LV_ALIGN_LEFT_MID, 4, 0);

        // Region text area for user input (e.g., US, EU, JP)
        regionTextArea = lv_textarea_create(region_wrapper);
        lv_obj_set_width(regionTextArea, 120);
        lv_textarea_set_one_line(regionTextArea, true);
        lv_textarea_set_max_length(regionTextArea, 50);
        lv_textarea_set_placeholder_text(regionTextArea, "e.g. US, EU");
        
        // Load current region from settings
        settings::SystemSettings sysSettings;
        if (settings::loadSystemSettings(sysSettings)) {
            lv_textarea_set_text(regionTextArea, sysSettings.region.c_str());
        }
        lv_obj_add_event_cb(regionTextArea, onRegionChanged, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_align(regionTextArea, LV_ALIGN_RIGHT_MID, 0, 0);

        // Language

        auto* language_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_width(language_wrapper, LV_PCT(100));
        lv_obj_set_height(language_wrapper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(language_wrapper, 8, 0);
        lv_obj_set_style_border_width(language_wrapper, 0, 0);

        auto* languageLabel = lv_label_create(language_wrapper);
        lv_label_set_text(languageLabel, textResources[i18n::Text::LANGUAGE].c_str());
        lv_obj_align(languageLabel, LV_ALIGN_LEFT_MID, 4, 0);

        languageDropdown = lv_dropdown_create(language_wrapper);
        lv_obj_set_width(languageDropdown, 150);
        lv_obj_align(languageDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        std::string language_options = getLanguageOptions();
        lv_dropdown_set_options(languageDropdown, language_options.c_str());
        lv_dropdown_set_selected(languageDropdown, static_cast<uint32_t>(settings::getLanguage()));
        lv_obj_add_event_cb(languageDropdown, onLanguageSet, LV_EVENT_VALUE_CHANGED, this);
    }

    void onHide(AppContext& app) override {
        if (settingsUpdated && regionTextArea) {
            settings::SystemSettings sysSettings;
            if (settings::loadSystemSettings(sysSettings)) {
                sysSettings.region = lv_textarea_get_text(regionTextArea);
                settings::saveSystemSettings(sysSettings);
            }
        }
    }
};

extern const AppManifest manifest = {
    .appId = "LocaleSettings",
    .appName = "Region & Language",
    .appIcon = TT_ASSETS_APP_ICON_TIME_DATE_SETTINGS,
    .appCategory = Category::Settings,
    .createApp = create<LocaleSettingsApp>
};

LaunchId start() {
    return app::start(manifest.appId);
}

} // namespace
