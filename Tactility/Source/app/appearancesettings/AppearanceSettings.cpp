#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/app/selectiondialog/SelectionDialog.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/Fonts.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Theme.h>
#include <Tactility/settings/AppearanceSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <format>
#include <string>

namespace tt::app::appearancesettings {

constexpr auto* TAG = "AppearanceSettings";

extern const ::AppManifest manifest;

namespace {

constexpr uint16_t MIN_FONT_SIZE = 10;
constexpr uint16_t MAX_FONT_SIZE = 32;
constexpr size_t APPLY_THREAD_STACK_SIZE = 8192;

enum class FontSlot {
    Regular,
    Mono
};

struct NamedColor {
    const char* name;
    lv_palette_t palette;
};

constexpr NamedColor PALETTE_COLORS[] = {
    { "Red", LV_PALETTE_RED },
    { "Pink", LV_PALETTE_PINK },
    { "Purple", LV_PALETTE_PURPLE },
    { "Deep purple", LV_PALETTE_DEEP_PURPLE },
    { "Indigo", LV_PALETTE_INDIGO },
    { "Blue", LV_PALETTE_BLUE },
    { "Light blue", LV_PALETTE_LIGHT_BLUE },
    { "Cyan", LV_PALETTE_CYAN },
    { "Teal", LV_PALETTE_TEAL },
    { "Green", LV_PALETTE_GREEN },
    { "Light green", LV_PALETTE_LIGHT_GREEN },
    { "Lime", LV_PALETTE_LIME },
    { "Yellow", LV_PALETTE_YELLOW },
    { "Amber", LV_PALETTE_AMBER },
    { "Orange", LV_PALETTE_ORANGE },
    { "Deep orange", LV_PALETTE_DEEP_ORANGE },
    { "Brown", LV_PALETTE_BROWN },
    { "Blue grey", LV_PALETTE_BLUE_GREY },
    { "Grey", LV_PALETTE_GREY },
};

constexpr size_t COLOR_COUNT = 3;
constexpr const char* COLOR_TITLES[COLOR_COUNT] = { "Primary color", "Secondary color", "Error color" };

struct ColorRowWidgets {
    lv_obj_t* row = nullptr;
    lv_obj_t* swatch = nullptr;
};

struct FontRowWidgets {
    lv_obj_t* defaultButton = nullptr;
    lv_obj_t* fileLabel = nullptr;
    lv_obj_t* warningLabel = nullptr;
};

struct Context {
    uint32_t appInstanceId = 0;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t selectRegularBit = 0;
    uint32_t selectMonoBit = 0;
    uint32_t selectColorBit = 0;
    uint32_t applyBit = 0;
    uint32_t cacheUpdatedBit = 0;

    settings::appearance::AppearanceSettings savedSettings;
    /** Accessed with the LVGL lock held */
    settings::appearance::AppearanceSettings pendingSettings;
    /** Characters in the pending font files, accessed with the LVGL lock held */
    size_t regularCharacterCount = 0;
    size_t monoCharacterCount = 0;
    bool applyFailed = false;
    std::atomic<bool> applying = false;

    Thread* applyThread = nullptr;
    lvgl::FontConfiguration applyConfiguration;
    std::atomic<bool> cacheUpdateSucceeded = false;

    uint32_t selectLaunchId = 0;
    /** The colour that the selection dialog is for */
    size_t selectColorIndex = 0;
    uint32_t colorSelectLaunchId = 0;
    FontSlot selectSlot = FontSlot::Regular;
    AppStream selectStream {};
    uint8_t selectBuffer[256] {};

    // Valid while the window is shown
    lv_obj_t* applyButton = nullptr;
    lv_obj_t* spinner = nullptr;
    FontRowWidgets regularRow;
    FontRowWidgets monoRow;
    lv_obj_t* errorLabel = nullptr;
    lv_obj_t* lightChip = nullptr;
    lv_obj_t* darkChip = nullptr;
    lv_obj_t* regularThemeChip = nullptr;
    lv_obj_t* monoThemeChip = nullptr;
    ColorRowWidgets colorRows[COLOR_COUNT];
    /** The display can only show the mono theme */
    bool isMonoDisplay = false;
};

lvgl::FontConfiguration toFontConfiguration(const settings::appearance::AppearanceSettings& settings) {
    return {
        .defaultSize = settings.fontSize != 0 ? settings.fontSize : static_cast<uint16_t>(TT_FONT_DEFAULT_SIZE),
        .regularFontPath = settings.regularFontPath,
        .monoFontPath = settings.monoFontPath,
    };
}

std::string& getFontPath(settings::appearance::AppearanceSettings& settings, FontSlot slot) {
    return slot == FontSlot::Regular ? settings.regularFontPath : settings.monoFontPath;
}

size_t& getCharacterCount(Context* ctx, FontSlot slot) {
    return slot == FontSlot::Regular ? ctx->regularCharacterCount : ctx->monoCharacterCount;
}

size_t getCharacterCount(const std::string& path) {
    return path.empty() ? 0 : lvgl::getTtfCharacterCount(path);
}

void setHidden(lv_obj_t* object, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

void updateFontRow(const FontRowWidgets& row, const std::string& path, size_t characterCount) {
    lv_label_set_text(row.fileLabel, path.empty() ? "System font" : file::getLastPathSegment(path).c_str());
    setHidden(row.defaultButton, path.empty());
    const bool large = !path.empty() && characterCount > lvgl::LARGE_FONT_CHARACTER_COUNT;
    if (large) {
        lv_label_set_text(row.warningLabel, std::format("Contains {} characters: creating the fonts can take long and use a lot of memory.", characterCount).c_str());
    }
    setHidden(row.warningLabel, !large);
}

bool isDeviceDefaultDark() {
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    return defaults.is_dark;
}

bool isDark(const settings::appearance::AppearanceSettings& settings) {
    switch (settings.themeMode) {
        case settings::appearance::ThemeMode::Light:
            return false;
        case settings::appearance::ThemeMode::Dark:
            return true;
        default:
            return isDeviceDefaultDark();
    }
}

void setChecked(lv_obj_t* object, bool checked) {
    if (checked) {
        lv_obj_add_state(object, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(object, LV_STATE_CHECKED);
    }
}

void updateColorRow(Context* ctx, size_t index);

void updateThemeWidgets(Context* ctx) {
    const bool dark = isDark(ctx->pendingSettings);
    setChecked(ctx->lightChip, !dark);
    setChecked(ctx->darkChip, dark);
    if (ctx->regularThemeChip != nullptr) {
        setChecked(ctx->regularThemeChip, !ctx->pendingSettings.monoTheme);
        setChecked(ctx->monoThemeChip, ctx->pendingSettings.monoTheme);
    }
    // The mono theme doesn't use colours
    for (size_t i = 0; i < COLOR_COUNT; i++) {
        if (ctx->colorRows[i].row != nullptr) {
            setHidden(ctx->colorRows[i].row, ctx->pendingSettings.monoTheme);
            updateColorRow(ctx, i);
        }
    }
}

/** Updates the widgets to the pending settings. Requires the LVGL lock. */
void updateWidgets(Context* ctx) {
    if (ctx->applyButton == nullptr) {
        return;
    }
    const bool applying = ctx->applying;
    setHidden(ctx->applyButton, applying || ctx->pendingSettings == ctx->savedSettings);
    setHidden(ctx->spinner, !applying);
    updateFontRow(ctx->regularRow, ctx->pendingSettings.regularFontPath, ctx->regularCharacterCount);
    updateFontRow(ctx->monoRow, ctx->pendingSettings.monoFontPath, ctx->monoCharacterCount);
    setHidden(ctx->errorLabel, !ctx->applyFailed);
    updateThemeWidgets(ctx);
}

void setDark(Context* ctx, bool dark) {
    // The device default is kept when it matches, so it doesn't count as a change
    if (dark == isDeviceDefaultDark()) {
        ctx->pendingSettings.themeMode = settings::appearance::ThemeMode::DeviceDefault;
    } else {
        ctx->pendingSettings.themeMode = dark ? settings::appearance::ThemeMode::Dark : settings::appearance::ThemeMode::Light;
    }
    updateWidgets(ctx);
}

void onLightPressed(lv_event_t* event) {
    setDark(static_cast<Context*>(lv_event_get_user_data(event)), false);
}

void onDarkPressed(lv_event_t* event) {
    setDark(static_cast<Context*>(lv_event_get_user_data(event)), true);
}

void onRegularThemePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->pendingSettings.monoTheme = false;
    updateWidgets(ctx);
}

void onMonoThemePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->pendingSettings.monoTheme = true;
    updateWidgets(ctx);
}

void onAnimationsChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* checkbox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->pendingSettings.animationsEnabled = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    updateWidgets(ctx);
}

uint32_t getPaletteColor(lv_palette_t palette) {
    return lv_color_to_u32(lv_palette_main(palette)) & 0xFFFFFF;
}

constexpr size_t PALETTE_COLOR_COUNT = sizeof(PALETTE_COLORS) / sizeof(PALETTE_COLORS[0]);

std::optional<uint32_t>& getColor(settings::appearance::AppearanceSettings& settings, size_t index) {
    switch (index) {
        case 0:
            return settings.primaryColor;
        case 1:
            return settings.secondaryColor;
        default:
            return settings.errorColor;
    }
}

uint32_t getDefaultColor(size_t index) {
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    const lv_color_t colors[COLOR_COUNT] = { defaults.color_primary, defaults.color_secondary, defaults.color_error };
    return lv_color_to_u32(colors[index]) & 0xFFFFFF;
}

void updateColorRow(Context* ctx, size_t index) {
    const auto& widgets = ctx->colorRows[index];
    const auto& color = getColor(ctx->pendingSettings, index);
    lv_obj_set_style_bg_color(widgets.swatch, lv_color_hex(color.value_or(getDefaultColor(index))), LV_STATE_DEFAULT);
}

void onColorButtonPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->selectColorIndex = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));
    task_event_group_signal(ctx->eventGroup, ctx->selectColorBit);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onApplyPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->applyBit);
}

void onFontSizeChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* spinbox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const auto size = static_cast<uint16_t>(lv_spinbox_get_value(spinbox));
    // The device's default size is stored as "unset", so it follows the device default
    ctx->pendingSettings.fontSize = size == TT_FONT_DEFAULT_SIZE ? 0 : size;
    updateWidgets(ctx);
}

void onFontSizeDecrementPressed(lv_event_t* event) {
    lv_spinbox_decrement(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
}

void onFontSizeIncrementPressed(lv_event_t* event) {
    lv_spinbox_increment(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
}

lv_obj_t* createStepButton(lv_obj_t* parent, const char* symbol, lv_event_cb_t callback, lv_obj_t* spinbox) {
    auto* button = lv_button_create(parent);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, spinbox);
    lv_obj_add_event_cb(button, callback, LV_EVENT_LONG_PRESSED_REPEAT, spinbox);
    return button;
}

void onSelectRegularPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectRegularBit);
}

void onSelectMonoPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectMonoBit);
}

void onDefaultRegularPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->pendingSettings.regularFontPath.clear();
    ctx->regularCharacterCount = 0;
    updateWidgets(ctx);
}

void onDefaultMonoPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->pendingSettings.monoFontPath.clear();
    ctx->monoCharacterCount = 0;
    updateWidgets(ctx);
}

/** An unstyled row, so it doesn't paint over the card. Its items are spaced like the card's content. */
lv_obj_t* createRow(lv_obj_t* parent) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, lv_obj_get_style_pad_column(parent, LV_PART_MAIN), LV_STATE_DEFAULT);
    // Room for the focus outline of the buttons, which is clipped to the row in compact mode
    lv_obj_set_style_pad_ver(row, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(row, 3, LV_STATE_DEFAULT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/** A title above a card, and the card */
lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
    auto* title_label = lv_label_create(parent);
    lv_label_set_text(title_label, title);

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    return card;
}

lv_obj_t* createLabeledRow(lv_obj_t* parent, const char* text) {
    auto* row = createRow(parent);
    auto* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_flex_grow(label, 1);
    return row;
}

lv_obj_t* createChip(lv_obj_t* parent, const char* text, lv_event_cb_t callback, Context* ctx) {
    auto* chip = lvgl_chip_create(parent);
    auto* label = lv_label_create(chip);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(chip, callback, LV_EVENT_SHORT_CLICKED, ctx);
    return chip;
}

ColorRowWidgets createColorRow(lv_obj_t* parent, size_t index, Context* ctx) {
    ColorRowWidgets widgets;
    // "Title        [swatch] [Change]"
    widgets.row = createLabeledRow(parent, COLOR_TITLES[index]);

    // The colour preview: its colour is the setting, its border uses the theme's text colour
    widgets.swatch = lv_obj_create(widgets.row);
    lv_obj_remove_style_all(widgets.swatch);
    lv_obj_remove_flag(widgets.swatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(widgets.swatch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(widgets.swatch, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(widgets.swatch, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(widgets.swatch, lv_obj_get_style_text_color(widgets.swatch, LV_PART_MAIN), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(widgets.swatch, LV_DPX(4), LV_STATE_DEFAULT);

    auto* button = lv_button_create(widgets.row);
    auto* button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Change");
    lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
    lv_obj_add_event_cb(button, onColorButtonPressed, LV_EVENT_SHORT_CLICKED, ctx);

    // As high as the button
    lv_obj_update_layout(button);
    const int32_t swatch_size = lv_obj_get_height(button);
    lv_obj_set_size(widgets.swatch, swatch_size, swatch_size);
    return widgets;
}

void createThemeCard(lv_obj_t* parent, Context* ctx) {
    auto* card = createSection(parent, "Theme");

    auto* mode_row = createLabeledRow(card, "Mode");
    ctx->lightChip = createChip(mode_row, "Light", onLightPressed, ctx);
    ctx->darkChip = createChip(mode_row, "Dark", onDarkPressed, ctx);

    // Monochrome and greyscale displays always use the mono theme, and it doesn't use colours
    if (!ctx->isMonoDisplay) {
        auto* style_row = createLabeledRow(card, "Style");
        ctx->regularThemeChip = createChip(style_row, "Regular", onRegularThemePressed, ctx);
        ctx->monoThemeChip = createChip(style_row, "Mono", onMonoThemePressed, ctx);
    }

    auto* animations_checkbox = lv_checkbox_create(card);
    lv_checkbox_set_text(animations_checkbox, "Animations");
    setChecked(animations_checkbox, ctx->pendingSettings.animationsEnabled);
    lv_obj_add_event_cb(animations_checkbox, onAnimationsChanged, LV_EVENT_VALUE_CHANGED, ctx);

    if (!ctx->isMonoDisplay) {
        for (size_t i = 0; i < COLOR_COUNT; i++) {
            ctx->colorRows[i] = createColorRow(card, i, ctx);
        }
    }
}

lv_obj_t* createButton(lv_obj_t* parent, const char* text, lv_event_cb_t callback, Context* ctx) {
    auto* button = lv_button_create(parent);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, ctx);
    return button;
}

FontRowWidgets createFontRow(lv_obj_t* parent, const char* title, lv_event_cb_t onSelect, lv_event_cb_t onDefault, Context* ctx) {
    FontRowWidgets widgets;
    // "Title              [Default]"
    auto* title_row = createRow(parent);
    auto* label = lv_label_create(title_row);
    lv_label_set_text(label, title);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_pad_ver(label, 0, LV_STATE_DEFAULT);
    widgets.defaultButton = createButton(title_row, "Default", onDefault, ctx);

    // "  file.ttf          [Select]"
    auto* file_row = createRow(parent);
    widgets.fileLabel = lv_label_create(file_row);
    lv_obj_set_flex_grow(widgets.fileLabel, 1);
    lv_label_set_long_mode(widgets.fileLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_pad_left(widgets.fileLabel, 12, LV_STATE_DEFAULT);
    createButton(file_row, "Select", onSelect, ctx);

    widgets.warningLabel = lv_label_create(parent);
    lv_obj_set_width(widgets.warningLabel, LV_PCT(100));
    lv_label_set_long_mode(widgets.warningLabel, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(widgets.warningLabel, lv_palette_main(LV_PALETTE_ORANGE), LV_STATE_DEFAULT);
    return widgets;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Appearance");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    ctx->spinner = lvgl_toolbar_add_spinner_action(toolbar);
    ctx->applyButton = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_OK, onApplyPressed, ctx);

    auto* content = lv_obj_create(parent);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(content, 0, LV_STATE_DEFAULT);

    const auto color_format = lv_display_get_color_format(lv_obj_get_display(parent));
    ctx->isMonoDisplay = color_format == LV_COLOR_FORMAT_I1 || color_format == LV_COLOR_FORMAT_L8;
    createThemeCard(content, ctx);

    auto* fonts_card = createSection(content, "Fonts");
    auto* font_size_row = createRow(fonts_card);
    auto* font_size_label = lv_label_create(font_size_row);
    lv_label_set_text(font_size_label, "Font size");
    lv_obj_set_flex_grow(font_size_label, 1);
    // [-] [size] [+]
    auto* font_size_spinbox = lv_spinbox_create(font_size_row);
    lv_spinbox_set_range(font_size_spinbox, MIN_FONT_SIZE, MAX_FONT_SIZE);
    lv_spinbox_set_digit_format(font_size_spinbox, 2, 0);
    lv_spinbox_set_step(font_size_spinbox, 1);
    const uint16_t font_size = ctx->pendingSettings.fontSize != 0 ? ctx->pendingSettings.fontSize : static_cast<uint16_t>(TT_FONT_DEFAULT_SIZE);
    lv_spinbox_set_value(font_size_spinbox, font_size);
    lv_obj_set_width(font_size_spinbox, lv_font_get_line_height(lv_obj_get_style_text_font(font_size_spinbox, LV_PART_MAIN)) * 3);
    lv_obj_set_style_text_align(font_size_spinbox, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // The value is changed with the buttons, so the digit cursor isn't shown
    lv_obj_set_style_bg_opa(font_size_spinbox, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_add_event_cb(font_size_spinbox, onFontSizeChanged, LV_EVENT_VALUE_CHANGED, ctx);
    auto* decrement_button = createStepButton(font_size_row, LV_SYMBOL_MINUS, onFontSizeDecrementPressed, font_size_spinbox);
    lv_obj_move_to_index(decrement_button, lv_obj_get_index(font_size_spinbox));
    createStepButton(font_size_row, LV_SYMBOL_PLUS, onFontSizeIncrementPressed, font_size_spinbox);

    ctx->regularRow = createFontRow(fonts_card, "Regular font", onSelectRegularPressed, onDefaultRegularPressed, ctx);
    ctx->monoRow = createFontRow(fonts_card, "Mono font", onSelectMonoPressed, onDefaultMonoPressed, ctx);

    ctx->errorLabel = lv_label_create(fonts_card);
    lv_obj_set_width(ctx->errorLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->errorLabel, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(ctx->errorLabel, "Failed to create the fonts. Check that the selected files are TrueType fonts.");
    lv_obj_set_style_text_color(ctx->errorLabel, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);

    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->applyButton = nullptr;
    ctx->spinner = nullptr;
    ctx->regularRow = {};
    ctx->monoRow = {};
    ctx->errorLabel = nullptr;
    ctx->lightChip = nullptr;
    ctx->darkChip = nullptr;
    ctx->regularThemeChip = nullptr;
    ctx->monoThemeChip = nullptr;
    for (auto& row : ctx->colorRows) {
        row = {};
    }
}

int32_t applyThreadMain(void* context) {
    auto* ctx = static_cast<Context*>(context);
    ctx->cacheUpdateSucceeded = lvgl::updateFontCache(ctx->applyConfiguration);
    task_event_group_signal(ctx->eventGroup, ctx->cacheUpdatedBit);
    return 0;
}

/** Ends an apply without a font error, keeping the changes pending */
void cancelApply(Context* ctx) {
    lvgl_lock();
    ctx->applying = false;
    updateWidgets(ctx);
    lvgl_unlock();
}

void finishApply(Context* ctx);

void startApply(Context* ctx) {
    if (ctx->applying || ctx->applyThread != nullptr) {
        return;
    }

    lvgl_lock();
    ctx->applyConfiguration = toFontConfiguration(ctx->pendingSettings);
    const bool fonts_changed = ctx->applyConfiguration != toFontConfiguration(ctx->savedSettings);
    ctx->applying = true;
    ctx->applyFailed = false;
    updateWidgets(ctx);
    lvgl_unlock();

    // Theme changes don't need new fonts
    if (!fonts_changed) {
        ctx->cacheUpdateSucceeded = true;
        finishApply(ctx);
        return;
    }

    ctx->applyThread = thread_alloc();
    if (ctx->applyThread == nullptr) {
        LOG_E(TAG, "Failed to create thread");
        cancelApply(ctx);
        return;
    }
    thread_set_name(ctx->applyThread, "font_cache");
    thread_set_stack_size(ctx->applyThread, APPLY_THREAD_STACK_SIZE);
    thread_set_main_function(ctx->applyThread, applyThreadMain, ctx);
    if (thread_start(ctx->applyThread) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start thread");
        thread_free(ctx->applyThread);
        ctx->applyThread = nullptr;
        cancelApply(ctx);
    }
}

void joinApplyThread(Context* ctx) {
    if (ctx->applyThread != nullptr) {
        thread_join(ctx->applyThread, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(ctx->applyThread);
        ctx->applyThread = nullptr;
    }
}

void finishApply(Context* ctx) {
    joinApplyThread(ctx);

    if (!ctx->cacheUpdateSucceeded) {
        LOG_E(TAG, "Failed to update the font cache");
        lvgl_lock();
        ctx->applying = false;
        ctx->applyFailed = true;
        updateWidgets(ctx);
        lvgl_unlock();
        return;
    }

    lvgl_lock();
    const auto applied_settings = ctx->pendingSettings;
    lvgl_unlock();
    if (!settings::appearance::save(applied_settings)) {
        // Fonts that aren't saved would revert on the next boot: keep the changes pending for a retry
        LOG_E(TAG, "Failed to save settings");
        cancelApply(ctx);
        return;
    }
    lvgl_lock();
    ctx->savedSettings = applied_settings;
    ctx->applying = false;
    lvgl_unlock();

    // The window is rebuilt from the saved settings when LVGL starts again
    LOG_I(TAG, "Restarting LVGL with the new settings");
    lvgl::stop();
    lvgl::configureTheme(applied_settings);
    lvgl::loadFonts(ctx->applyConfiguration);
    lvgl::start();
}

void startFileSelection(Context* ctx, FontSlot slot) {
    if (ctx->selectLaunchId != 0 || ctx->applying) {
        return;
    }
    ctx->selectSlot = slot;
    ctx->selectLaunchId = fileselection::startForExistingFile(ctx->appInstanceId, ctx->selectStream, ctx->selectBuffer, sizeof(ctx->selectBuffer), ctx->eventGroup);
}

void startColorSelection(Context* ctx) {
    if (ctx->colorSelectLaunchId != 0 || ctx->applying) {
        return;
    }
    std::vector<std::string> items;
    items.reserve(PALETTE_COLOR_COUNT + 1);
    items.emplace_back("Default");
    for (const auto& named_color : PALETTE_COLORS) {
        items.emplace_back(named_color.name);
    }
    ctx->colorSelectLaunchId = selectiondialog::start(ctx->appInstanceId, COLOR_TITLES[ctx->selectColorIndex], items);
}

void onColorSelectionResult(Context* ctx, const AppEvent& event) {
    ctx->colorSelectLaunchId = 0;
    // Index 0 is "Default", the others are the palette colours. Other results mean that the dialog was dismissed.
    const int32_t selected = event.result.result;
    if (selected >= 0 && selected <= static_cast<int32_t>(PALETTE_COLOR_COUNT)) {
        lvgl_lock();
        auto& color = getColor(ctx->pendingSettings, ctx->selectColorIndex);
        if (selected == 0) {
            color.reset();
        } else {
            color = getPaletteColor(PALETTE_COLORS[selected - 1].palette);
        }
        updateWidgets(ctx);
        lvgl_unlock();
    }
    app_manager_stop(event.result.launch_id);
}

void onFileSelectionResult(Context* ctx, const AppEvent& event) {
    ctx->selectLaunchId = 0;
    if (event.result.result == 0 /* Ok */) {
        char path[sizeof(ctx->selectBuffer)];
        const size_t length = app_stream_read(&ctx->selectStream, path, sizeof(path));
        app_stream_unsubscribe(&ctx->selectStream);
        if (length > 0) {
            const auto selected_path = std::string(path, length);
            const size_t character_count = getCharacterCount(selected_path);
            lvgl_lock();
            getFontPath(ctx->pendingSettings, ctx->selectSlot) = selected_path;
            getCharacterCount(ctx, ctx->selectSlot) = character_count;
            updateWidgets(ctx);
            lvgl_unlock();
        }
    } else {
        app_stream_unsubscribe(&ctx->selectStream);
    }
    app_manager_stop(event.result.launch_id);
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx;
    ctx.appInstanceId = app_scheduler_current_app_id();
    ctx.savedSettings = settings::appearance::loadOrGetDefault();
    ctx.pendingSettings = ctx.savedSettings;
    ctx.regularCharacterCount = getCharacterCount(ctx.pendingSettings.regularFontPath);
    ctx.monoCharacterCount = getCharacterCount(ctx.pendingSettings.monoFontPath);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.selectRegularBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.selectMonoBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.selectColorBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.applyBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.cacheUpdatedBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool should_close = false;
    while (!should_close) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.selectRegularBit) {
            startFileSelection(&ctx, FontSlot::Regular);
        }
        if (flags & ctx.selectMonoBit) {
            startFileSelection(&ctx, FontSlot::Mono);
        }
        if (flags & ctx.selectColorBit) {
            startColorSelection(&ctx);
        }
        if (flags & ctx.applyBit) {
            startApply(&ctx);
        }
        if (flags & ctx.cacheUpdatedBit) {
            finishApply(&ctx);
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                should_close = true;
            } else if (event.type == APP_EVENT_RESULT && event.result.launch_id == ctx.selectLaunchId) {
                onFileSelectionResult(&ctx, event);
            } else if (event.type == APP_EVENT_RESULT && event.result.launch_id == ctx.colorSelectLaunchId) {
                onColorSelectionResult(&ctx, event);
            }
        }
    }

    // The thread uses the context
    joinApplyThread(&ctx);

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.appearance",
    .name = "Appearance",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::appearancesettings
