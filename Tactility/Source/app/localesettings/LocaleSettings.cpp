#include <Tactility/Tactility.h>

#include <Tactility/RecursiveMutex.h>
#include <Tactility/app/localesettings/TextResources.h>
#include <Tactility/app/selectiondialog/SelectionDialog.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <map>
#include <string>
#include <vector>

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
    TaskEventGroup* eventGroup = nullptr;
    uint32_t selectLanguageBit = 0;
    uint32_t languageSelectLaunchId = 0;
    // Valid while the window is shown
    lv_obj_t* languageLabel = nullptr;
    lv_obj_t* languageValueLabel = nullptr;
    bool settingsUpdated = false;

    std::map<settings::Language, std::string> languageMap;
};


std::vector<std::string> getLanguageNames(Context* ctx) {
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
    return items;
}

/** Requires the LVGL lock */
void updateViews(Context* ctx) {
    if (ctx->languageLabel == nullptr) {
        return;
    }
    lv_label_set_text(ctx->languageLabel, ctx->textResources[i18n::Text::LANGUAGE].c_str());
    const auto names = getLanguageNames(ctx);
    const auto index = static_cast<size_t>(settings::getLanguage());
    lv_label_set_text(ctx->languageValueLabel, index < names.size() ? names[index].c_str() : "");
}

void onSelectLanguagePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectLanguageBit);
}

void startLanguageSelection(Context* ctx) {
    if (ctx->languageSelectLaunchId != 0) {
        return;
    }
    ctx->languageSelectLaunchId = selectiondialog::start(ctx->appInstanceId, ctx->textResources[i18n::Text::LANGUAGE], getLanguageNames(ctx));
}

void onLanguageSelectionResult(Context* ctx, const AppEvent& event) {
    ctx->languageSelectLaunchId = 0;
    // Other results mean that the dialog was dismissed
    const int32_t selected = event.result.result;
    if (selected >= 0 && selected < static_cast<int32_t>(settings::Language::count)) {
        settings::setLanguage(static_cast<settings::Language>(selected));
        ctx->textResources.load();
        lvgl_lock();
        updateViews(ctx);
        lvgl_unlock();
    }
    app_manager_stop(event.result.launch_id);
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
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->textResources.load();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Locale");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // "Language           English [Select]"
    auto* card = lvgl_card_create(main_wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    // Transparent, so the card provides the background
    auto* language_row = lv_obj_create(card);
    lv_obj_set_size(language_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(language_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(language_row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(language_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(language_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(language_row, 0, LV_STATE_DEFAULT);
    lv_obj_remove_flag(language_row, LV_OBJ_FLAG_SCROLLABLE);

    // The title with the language below it, which take the width that the button leaves. Texts that don't fit scroll.
    auto* texts = lv_obj_create(language_row);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(texts, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(texts, LV_OBJ_FLAG_SCROLLABLE);

    ctx->languageLabel = lv_label_create(texts);
    lv_obj_set_width(ctx->languageLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->languageLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    ctx->languageValueLabel = lv_label_create(texts);
    lv_obj_set_width(ctx->languageValueLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->languageValueLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(ctx->languageValueLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    auto* select_button = lv_button_create(language_row);
    lv_label_set_text(lv_label_create(select_button), "Change");
    lv_obj_add_event_cb(select_button, onSelectLanguagePressed, LV_EVENT_SHORT_CLICKED, ctx);

    updateViews(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->languageLabel = nullptr;
    ctx->languageValueLabel = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx;
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.selectLanguageBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.selectLanguageBit) {
            startLanguageSelection(&ctx);
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            } else if (event.type == APP_EVENT_RESULT && event.result.launch_id == ctx.languageSelectLaunchId) {
                onLanguageSelectionResult(&ctx, event);
            }
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.localesettings",
    .name = "Locale",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::localesettings
