
#include <Tactility/StringUtils.h>
#include <Tactility/i18n/Localisation.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/step_indicator.h>
#include <lvgl.h>

// Included last, as it makes std:: resolve to tt::std
#include <Tactility/app/setup/SetupPrivate.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::app::setup {


/** The language and welcome pages count as the first two steps, followed by the configuration steps */
uint32_t getStepCount(const Context* ctx) {
    return static_cast<uint32_t>(ctx->steps.size()) + 2;
}

/** The steps before the current one, or all of them once setup is done */
static uint32_t getFinishedStepCount(const Context* ctx) {
    switch (ctx->phase) {
        case Phase::Language:
            return 0;
        case Phase::Welcome:
            return 1;
        case Phase::StepIntro:
        case Phase::StepChoice:
        case Phase::WifiSetup:
            return static_cast<uint32_t>(ctx->stepIndex) + 2;
        case Phase::Done:
            return getStepCount(ctx);
    }
    return 0;
}

static StepConfiguration::Content getContent(const Context* ctx) {
    switch (ctx->phase) {
        case Phase::Language:
            return StepConfiguration::Content::Languages;
        case Phase::StepIntro: {
            const auto& step = ctx->steps[ctx->stepIndex];
            // The choices are on their own page
            if (!step.choiceTitle.empty()) {
                return StepConfiguration::Content::Description;
            }
            return step.content;
        }
        case Phase::StepChoice:
            return StepConfiguration::Content::Choice;
        case Phase::WifiSetup:
            return StepConfiguration::Content::Wifi;
        default:
            return StepConfiguration::Content::Description;
    }
}

/** Shows which choice of the current step is picked */
static void renderChoiceChips(Context* ctx) {
    const int32_t choice = ctx->steps[ctx->stepIndex].choice;
    const uint32_t count = lv_obj_get_child_count(ctx->choiceChips);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_set_state(lv_obj_get_child(ctx->choiceChips, static_cast<int32_t>(i)), LV_STATE_CHECKED, static_cast<int32_t>(i) == choice);
    }
}

static void onChoiceChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->steps[ctx->stepIndex].choice = static_cast<int32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    renderChoiceChips(ctx);
    lv_obj_remove_state(ctx->continueButton, LV_STATE_DISABLED);
}

/** Replaces the chips with the choices of the current step */
static void createChoiceChips(Context* ctx) {
    lv_obj_clean(ctx->choiceChips);
    const auto& choices = ctx->steps[ctx->stepIndex].choices;
    for (size_t i = 0; i < choices.size(); i++) {
        auto* chip = lvgl_chip_create(ctx->choiceChips);
        lv_label_set_text(lv_label_create(chip), choices[i].c_str());
        lv_obj_set_user_data(chip, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(chip, onChoiceChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
}

static void onLanguageClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->languageStep.choice = static_cast<int32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
    // Sets the language, which shows the page again in it
    ctx->onLanguagePicked(ctx->languageStep.choice);
}

void renderLanguages(Context* ctx) {
    auto* container = ctx->wifiContainer;
    lv_obj_clean(container);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_state(ctx->continueButton, LV_STATE_DISABLED, ctx->languageStep.choice < 0);

    auto* card = lvgl_card_create(container);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
    // The list items' pressed and focused backgrounds follow the card's rounded corners
    lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);
    auto* list = lvgl_list_create(card);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);

    const auto& names = ctx->languageStep.choices;
    for (size_t i = 0; i < names.size(); i++) {
        auto* button = lvgl_list_add_button(list, static_cast<int32_t>(i) == ctx->languageStep.choice ? LV_SYMBOL_OK : nullptr, names[i].c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(button, onLanguageClicked, LV_EVENT_SHORT_CLICKED, ctx);
    }
    lvgl_list_hide_last_divider(card);
}

/** Small widgets get smaller titles */
enum LvglFontSize getTitleFontSize() {
    const auto size = lvgl_theme_get_size();
    if (size <= 20) {
        return FONT_SIZE_SMALL;
    }
    return size < 50 ? FONT_SIZE_DEFAULT : FONT_SIZE_LARGE;
}

/**
 * The time zone page shows the title at the top and the list below it, the other pages center their text.
 * Pages that don't fit above the buttons scroll.
 */
static void renderLayout(Context* ctx) {
    const auto content = getContent(ctx);
    const bool showsTimeZones = content == StepConfiguration::Content::TimeZones;
    const bool showsWifi = content == StepConfiguration::Content::Wifi;
    const bool showsLanguages = content == StepConfiguration::Content::Languages;
    // The Wi-Fi container holds the Wi-Fi list and the language list
    const bool showsList = showsWifi || showsLanguages;
    const bool showsChoice = content == StepConfiguration::Content::Choice;
    const bool hasTimeZoneWidgets = lv_obj_get_child_count(ctx->timeZoneContainer) > 0;
    lv_obj_set_hidden(ctx->descriptionLabel, showsTimeZones || showsList || ctx->phase == Phase::StepChoice);
    lv_obj_set_hidden(ctx->timeZoneContainer, !showsTimeZones);
    lv_obj_set_hidden(ctx->wifiContainer, !showsList);
    lv_obj_set_hidden(ctx->choiceChips, !showsChoice);
    // The group skips disabled widgets, but not hidden ones
    lv_obj_set_state(ctx->choiceChips, LV_STATE_DISABLED, !showsChoice);
    lv_obj_scroll_to_y(ctx->content, 0, LV_ANIM_OFF);

    lv_area_t parent;
    lv_area_t bottom;
    if (!showsList) {
        lv_obj_clean(ctx->wifiContainer);
    }
    if (!showsTimeZones && !showsList) {
        if (hasTimeZoneWidgets) {
            lv_obj_clean(ctx->timeZoneContainer);
            ctx->timeZoneView->destroyWidgets();
        }
        const int titleMargin = lvgl_get_text_font_height(getTitleFontSize());
        if (showsChoice) {
            createChoiceChips(ctx);
            // Continue confirms the picked choice
            renderChoiceChips(ctx);
            lv_obj_set_state(ctx->continueButton, LV_STATE_DISABLED, ctx->steps[ctx->stepIndex].choice < 0);
        } else {
            lv_obj_remove_state(ctx->continueButton, LV_STATE_DISABLED);
        }
        lv_obj_set_style_pad_row(ctx->contentInner, showsChoice ? titleMargin / 2 : titleMargin, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(ctx->contentInner, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_min_height(ctx->contentInner, LV_PCT(100), LV_STATE_DEFAULT);
        // Fills the space above the buttons
        lv_obj_update_layout(lv_obj_get_parent(ctx->content));
        lv_obj_get_content_coords(lv_obj_get_parent(ctx->content), &parent);
        lv_obj_get_coords(ctx->bottom, &bottom);
        lv_obj_set_height(ctx->content, bottom.y1 - parent.y1);
    } else {
        // The list fills the space between the title and the buttons
        auto* listContainer = showsList ? ctx->wifiContainer : ctx->timeZoneContainer;
        if (showsList) {
            if (hasTimeZoneWidgets) {
                lv_obj_clean(ctx->timeZoneContainer);
                ctx->timeZoneView->destroyWidgets();
            }
            if (showsLanguages) {
                renderLanguages(ctx);
            } else {
                renderWifi(ctx);
            }
        } else {
            if (!hasTimeZoneWidgets) {
                ctx->timeZoneView->createWidgets(ctx->timeZoneContainer);
            }
            // Continue confirms the picked time zone
            lv_obj_set_state(ctx->continueButton, LV_STATE_DISABLED, ctx->timeZoneName.empty());
        }

        lv_obj_set_style_pad_row(ctx->contentInner, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(ctx->contentInner, LV_DPX(8), LV_STATE_DEFAULT);
        lv_obj_set_style_min_height(ctx->contentInner, 0, LV_STATE_DEFAULT);
        lv_obj_set_height(ctx->content, LV_SIZE_CONTENT);

        lv_obj_update_layout(lv_obj_get_parent(listContainer));
        lv_area_t title;
        lv_obj_get_coords(ctx->content, &title);
        lv_obj_get_coords(ctx->bottom, &bottom);
        lv_obj_get_content_coords(lv_obj_get_parent(listContainer), &parent);
        lv_obj_set_pos(listContainer, 0, title.y2 + LV_DPX(4) - parent.y1);
        lv_obj_set_size(listContainer, LV_PCT(100), bottom.y1 - title.y2 - LV_DPX(8));
    }

    // Only a page that doesn't fit takes the focus, so the keys can scroll it
    lv_obj_update_layout(ctx->content);
    const bool scrolls = lv_obj_get_scroll_bottom(ctx->content) > 0 || lv_obj_get_scroll_top(ctx->content) > 0;
    lv_obj_set_state(ctx->content, LV_STATE_DISABLED, !scrolls);
}

void renderCurrent(Context* ctx) {
    lvgl_step_indicator_set_finished_count(ctx->stepIndicator, getFinishedStepCount(ctx));
    lv_obj_set_hidden(ctx->backButton, ctx->phase == Phase::Language);
    lv_label_set_text(lv_obj_get_child(ctx->backButton, 0), tr("Back"));
    switch (ctx->phase) {
        case Phase::Language:
            lv_label_set_text(ctx->titleLabel, tr("Language"));
            lv_obj_set_hidden(ctx->skipButton, true);
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), tr("Continue"));
            break;
        case Phase::Welcome: {
            lv_label_set_text(ctx->titleLabel, tr("Welcome"));
            auto device_names = string::split(::std::string(CONFIG_TT_DEVICE_NAME_SIMPLE), ",");
            lv_label_set_text(ctx->descriptionLabel, i18n::format(tr("It's time to set up your {}!"), { device_names.front() }).c_str());
            lv_obj_set_hidden(ctx->skipButton, true);
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), tr("Continue"));
            break;
        }
        case Phase::StepIntro: {
            const auto& step = ctx->steps[ctx->stepIndex];
            lv_label_set_text(ctx->titleLabel, step.title.c_str());
            lv_label_set_text(ctx->descriptionLabel, step.description.c_str());
            lv_obj_set_hidden(ctx->skipButton, false);
            lv_label_set_text(lv_obj_get_child(ctx->skipButton, 0), tr("Skip"));
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), tr("Continue"));
            break;
        }
        case Phase::StepChoice: {
            lv_label_set_text(ctx->titleLabel, ctx->steps[ctx->stepIndex].choiceTitle.c_str());
            lv_obj_set_hidden(ctx->skipButton, false);
            lv_label_set_text(lv_obj_get_child(ctx->skipButton, 0), tr("Skip"));
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), tr("Continue"));
            break;
        }
        case Phase::WifiSetup: {
            lv_label_set_text(ctx->titleLabel, ctx->steps[ctx->stepIndex].title.c_str());
            lv_obj_set_hidden(ctx->skipButton, false);
            lv_label_set_text(lv_obj_get_child(ctx->skipButton, 0), tr("Skip"));
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), tr("Continue"));
            break;
        }
        case Phase::Done:
            lv_label_set_text(ctx->titleLabel, tr("Setup Complete"));
            lv_label_set_text(ctx->descriptionLabel, tr("You're all set."));
            lv_obj_set_hidden(ctx->skipButton, true);
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), tr("Finish"));
            break;
    }
    renderLayout(ctx);
    // Without a touch screen, the picked choice has the focus, or the first one
    if (ctx->phase == Phase::StepChoice && !lvgl_indev_exists(LV_INDEV_TYPE_POINTER) && lv_obj_get_child_count(ctx->choiceChips) > 0) {
        const int32_t choice = ctx->steps[ctx->stepIndex].choice;
        lv_group_focus_obj(ctx->choiceChips);
        lv_gridnav_set_focused(ctx->choiceChips, lv_obj_get_child(ctx->choiceChips, choice < 0 ? 0 : choice), LV_ANIM_OFF);
    }
    // Without a touch screen, Finish has the focus
    if (ctx->phase == Phase::Done && !lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        auto* buttonRow = lv_obj_get_parent(ctx->continueButton);
        lv_group_focus_obj(buttonRow);
        lv_gridnav_set_focused(buttonRow, ctx->continueButton, LV_ANIM_OFF);
    }
}

}
