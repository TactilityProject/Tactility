#include <Tactility/Tactility.h>

#include <Tactility/RecursiveMutex.h>
#include <Tactility/StringUtils.h>
#include <Tactility/app/localesettings/TextResources.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>

#include <lvgl.h>
#include <map>

namespace tt::app::localesettings {

constexpr auto* TAG = "LocaleSettings";

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/LocaleSettings/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/LocaleSettings/i18n";
#endif

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    tt::i18n::TextResources textResources = tt::i18n::TextResources(TEXT_RESOURCE_PATH);
    RecursiveMutex mutex;
    lv_obj_t* languageDropdown = nullptr;
    bool settingsUpdated = false;

    std::map<settings::Language, std::string> languageMap;
};


std::string getLanguageOptions(Context* ctx) {
    std::vector<std::string> items;
    for (int i = 0; i < static_cast<int>(settings::Language::count); i++) {
        switch (static_cast<settings::Language>(i)) {
            case settings::Language::en_GB:
                items.push_back(ctx->textResources[i18n::Text::EN_GB]);
                break;
            case settings::Language::en_US:
                items.push_back(ctx->textResources[i18n::Text::EN_US]);
                break;
            case settings::Language::fr_FR:
                items.push_back(ctx->textResources[i18n::Text::FR_FR]);
                break;
            case settings::Language::nl_BE:
                items.push_back(ctx->textResources[i18n::Text::NL_BE]);
                break;
            case settings::Language::nl_NL:
                items.push_back(ctx->textResources[i18n::Text::NL_NL]);
                break;
            case settings::Language::count:
                break;
        }
    }
    return string::join(items, "\n");
}

void updateViews(Context* ctx) {
    ctx->textResources.load();

    std::string language_options = getLanguageOptions(ctx);
    lv_dropdown_set_options(ctx->languageDropdown, language_options.c_str());
    lv_dropdown_set_selected(ctx->languageDropdown, static_cast<uint32_t>(settings::getLanguage()));
}

void onLanguageSet(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto index = lv_dropdown_get_selected(dropdown);
    auto language = static_cast<settings::Language>(index);
    settings::setLanguage(language);

    updateViews(ctx);
}

// Preserved from the pre-conversion code as-is: declared but never wired to any widget there
// either, so this has always been dead code (kept verbatim rather than dropped, since removing
// it would be a functional judgment call outside the scope of this lifecycle-only conversion).
[[maybe_unused]] void onRegionChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->settingsUpdated = true;
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->textResources.load();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Region & Language");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // Language

    auto* language_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_width(language_wrapper, LV_PCT(100));
    lv_obj_set_height(language_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(language_wrapper, 8, 0);
    lv_obj_set_style_border_width(language_wrapper, 0, 0);

    auto* languageLabel = lv_label_create(language_wrapper);
    lv_label_set_text(languageLabel, ctx->textResources[i18n::Text::LANGUAGE].c_str());
    lv_obj_align(languageLabel, LV_ALIGN_LEFT_MID, 4, 0);

    ctx->languageDropdown = lv_dropdown_create(language_wrapper);
    lv_obj_set_width(ctx->languageDropdown, 150);
    lv_obj_align(ctx->languageDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    std::string language_options = getLanguageOptions(ctx);
    lv_dropdown_set_options(ctx->languageDropdown, language_options.c_str());
    lv_dropdown_set_selected(ctx->languageDropdown, static_cast<uint32_t>(settings::getLanguage()));
    lv_obj_add_event_cb(ctx->languageDropdown, onLanguageSet, LV_EVENT_VALUE_CHANGED, ctx);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx;
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

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "LocaleSettings",
    .name = "Region & Language",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::localesettings
