#define L10N_APP "appearancesettings"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/dialog/Dialog.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/Fonts.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Theme.h>
#include <lvgl_window_manager/system_bars.h>
#include <Tactility/lvgl/FontSizes.h>
#include <Tactility/settings/AppearanceSettings.h>
#include <Tactility/settings/LauncherSettings.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/dialog.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/sliderbox.h>
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
    { tr_noop("Red"), LV_PALETTE_RED },
    { tr_noop("Pink"), LV_PALETTE_PINK },
    { tr_noop("Purple"), LV_PALETTE_PURPLE },
    { tr_noop("Deep purple"), LV_PALETTE_DEEP_PURPLE },
    { tr_noop("Indigo"), LV_PALETTE_INDIGO },
    { tr_noop("Blue"), LV_PALETTE_BLUE },
    { tr_noop("Light blue"), LV_PALETTE_LIGHT_BLUE },
    { tr_noop("Cyan"), LV_PALETTE_CYAN },
    { tr_noop("Teal"), LV_PALETTE_TEAL },
    { tr_noop("Green"), LV_PALETTE_GREEN },
    { tr_noop("Light green"), LV_PALETTE_LIGHT_GREEN },
    { tr_noop("Lime"), LV_PALETTE_LIME },
    { tr_noop("Yellow"), LV_PALETTE_YELLOW },
    { tr_noop("Amber"), LV_PALETTE_AMBER },
    { tr_noop("Orange"), LV_PALETTE_ORANGE },
    { tr_noop("Deep orange"), LV_PALETTE_DEEP_ORANGE },
    { tr_noop("Brown"), LV_PALETTE_BROWN },
    { tr_noop("Blue grey"), LV_PALETTE_BLUE_GREY },
    { tr_noop("Grey"), LV_PALETTE_GREY },
};

constexpr size_t COLOR_COUNT = 4;
constexpr size_t SURFACE_COLOR_INDEX = 2;
constexpr const char* COLOR_TITLES[COLOR_COUNT] = { tr_noop("Primary color"), tr_noop("Secondary color"), tr_noop("Surface color"), tr_noop("Error color") };
// In tint level order (see settings::appearance::getSurfaceColor())
constexpr const char* SURFACE_TINT_NAMES[settings::appearance::SURFACE_TINT_LEVEL_COUNT] = { tr_noop("Low"), tr_noop("Medium"), tr_noop("High") };

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

    /** Signaled by the file dialog, which stores the path or an empty one in selectedPath first */
    uint32_t fileResultBit = 0;
    std::string selectedPath;
    /** The colour that the selection dialog is for */
    size_t selectColorIndex = 0;
    /** Signaled by the colour selection dialog, which stores the selected index in colorResult first */
    uint32_t colorResultBit = 0;
    int32_t colorResult = WINDOW_DIALOG_DISMISSED;
    FontSlot selectSlot = FontSlot::Regular;

    /** The scroll position of the content, kept while e.g. a selection dialog covers the window. Accessed with the LVGL lock held. */
    int32_t contentScrollY = 0;

    // Valid while the window is shown
    lv_obj_t* content = nullptr;
    lv_obj_t* applyButton = nullptr;
    lv_obj_t* spinner = nullptr;
    FontRowWidgets regularRow;
    FontRowWidgets monoRow;
    lv_obj_t* errorLabel = nullptr;
    lv_obj_t* lightChip = nullptr;
    lv_obj_t* darkChip = nullptr;
    lv_obj_t* spacingSlider = nullptr;
    lv_obj_t* sizeSlider = nullptr;
    lv_obj_t* regularThemeChip = nullptr;
    lv_obj_t* monoThemeChip = nullptr;
    // Shown for the regular theme: the "Theme Colors" title and card
    lv_obj_t* colorsTitle = nullptr;
    lv_obj_t* colorsCard = nullptr;
    ColorRowWidgets colorRows[COLOR_COUNT];
    /** Shown when a surface colour is set */
    lv_obj_t* surfaceTintRow = nullptr;
    lv_obj_t* surfaceTintChips[settings::appearance::SURFACE_TINT_LEVEL_COUNT] = {};
    /** The display can only show the mono theme */
    bool isMonoDisplay = false;
    // System bars, which are applied right away instead of with the Apply button
    lv_obj_t* systemBarsAutoChip = nullptr;
    lv_obj_t* systemBarsCustomChip = nullptr;
    lv_obj_t* portraitRow = nullptr;
    lv_obj_t* portraitSplitChip = nullptr;
    lv_obj_t* portraitSideChip = nullptr;
    lv_obj_t* landscapeRow = nullptr;
    lv_obj_t* landscapeSplitChip = nullptr;
    lv_obj_t* landscapeSideChip = nullptr;
    lv_obj_t* alignLeftChip = nullptr;
    lv_obj_t* alignRightChip = nullptr;
};

lvgl::FontConfiguration toFontConfiguration(const settings::appearance::AppearanceSettings& settings) {
    // The font settings aren't shown without enough memory to generate fonts, so saved values are ignored like at boot
    if (!lvgl::isFontChangeSupported()) {
        return { .defaultSize = lvgl::getDefaultFontSize(), .regularFontPath = "", .monoFontPath = "" };
    }
    return {
        .defaultSize = settings.fontSize != 0 ? settings.fontSize : lvgl::getDefaultFontSize(),
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
        lv_obj_set_hidden(object, true);
    } else {
        lv_obj_set_hidden(object, false);
    }
}

void updateFontRow(const FontRowWidgets& row, const std::string& path, size_t characterCount) {
    lv_label_set_text(row.fileLabel, path.empty() ? tr("System font") : file::getLastPathSegment(path).c_str());
    setHidden(row.defaultButton, path.empty());
    const bool large = !path.empty() && characterCount > lvgl::LARGE_FONT_CHARACTER_COUNT;
    if (large) {
        lv_label_set_text(row.warningLabel, i18n::format(tr("Contains {} characters: creating the fonts can take long and use a lot of memory."), { std::to_string(characterCount) }).c_str());
    }
    setHidden(row.warningLabel, !large);
}

bool isDeviceDefaultDark() {
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    return defaults.is_dark;
}

uint8_t getSpacing(const settings::appearance::AppearanceSettings& settings) {
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    return settings.spacing.value_or(defaults.spacing);
}

uint8_t getSize(const settings::appearance::AppearanceSettings& settings) {
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    return settings.size.value_or(defaults.size);
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
uint32_t getDefaultColor(size_t index);

void updateSurfaceTintRow(Context* ctx) {
    if (ctx->surfaceTintRow == nullptr) {
        return;
    }
    const auto& settings = ctx->pendingSettings;
    setHidden(ctx->surfaceTintRow, !settings.surfaceColor.has_value());
    for (uint8_t i = 0; i < settings::appearance::SURFACE_TINT_LEVEL_COUNT; i++) {
        setChecked(ctx->surfaceTintChips[i], settings.surfaceTintLevel == i);
    }
}

void updateThemeWidgets(Context* ctx) {
    const bool dark = isDark(ctx->pendingSettings);
    setChecked(ctx->lightChip, !dark);
    setChecked(ctx->darkChip, dark);
    if (ctx->spacingSlider != nullptr) {
        lvgl_sliderbox_set_value(ctx->spacingSlider, getSpacing(ctx->pendingSettings), LV_ANIM_OFF);
        lvgl_sliderbox_set_value(ctx->sizeSlider, getSize(ctx->pendingSettings), LV_ANIM_OFF);
    }
    if (ctx->regularThemeChip != nullptr) {
        setChecked(ctx->regularThemeChip, !ctx->pendingSettings.monoTheme);
        setChecked(ctx->monoThemeChip, ctx->pendingSettings.monoTheme);
    }
    // The mono theme doesn't use colours
    if (ctx->colorsCard != nullptr) {
        setHidden(ctx->colorsTitle, ctx->pendingSettings.monoTheme);
        setHidden(ctx->colorsCard, ctx->pendingSettings.monoTheme);
        for (size_t i = 0; i < COLOR_COUNT; i++) {
            updateColorRow(ctx, i);
        }
    }
    updateSurfaceTintRow(ctx);
}

/** Updates the widgets to the pending settings. Requires the LVGL lock. */
void updateWidgets(Context* ctx) {
    if (ctx->applyButton == nullptr) {
        return;
    }
    const bool applying = ctx->applying;
    setHidden(ctx->applyButton, applying || ctx->pendingSettings == ctx->savedSettings);
    setHidden(ctx->spinner, !applying);
    if (ctx->errorLabel != nullptr) {
        updateFontRow(ctx->regularRow, ctx->pendingSettings.regularFontPath, ctx->regularCharacterCount);
        updateFontRow(ctx->monoRow, ctx->pendingSettings.monoFontPath, ctx->monoCharacterCount);
        setHidden(ctx->errorLabel, !ctx->applyFailed);
    }
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

void onSpacingChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    const auto value = static_cast<uint8_t>(lvgl_sliderbox_get_value(ctx->spacingSlider));
    // The device default is kept when it matches, so it doesn't count as a change
    ctx->pendingSettings.spacing = value == defaults.spacing ? std::nullopt : std::optional<uint8_t>(value);
    updateWidgets(ctx);
}

void onSizeChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    const auto value = static_cast<uint8_t>(lvgl_sliderbox_get_value(ctx->sizeSlider));
    ctx->pendingSettings.size = value == defaults.size ? std::nullopt : std::optional<uint8_t>(value);
    updateWidgets(ctx);
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

void onSurfaceTintPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->pendingSettings.surfaceTintLevel = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    updateWidgets(ctx);
}

void onAnimationsChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* animations_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->pendingSettings.animationsEnabled = lv_obj_has_state(animations_switch, LV_STATE_CHECKED);
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
        case SURFACE_COLOR_INDEX:
            return settings.surfaceColor;
        default:
            return settings.errorColor;
    }
}

/** The surface has no default colour of its own: the theme derives it */
uint32_t getDefaultColor(size_t index) {
    LvglThemeSettings defaults;
    lvgl_theme_get_default_settings(&defaults);
    const lv_color_t color = index == 0 ? defaults.color_primary : (index == 1 ? defaults.color_secondary : defaults.color_error);
    return lv_color_to_u32(color) & 0xFFFFFF;
}

void updateColorRow(Context* ctx, size_t index) {
    const auto& widgets = ctx->colorRows[index];
    const auto& color = getColor(ctx->pendingSettings, index);
    uint32_t shown;
    if (color.has_value()) {
        // The picked colour: the surface's tinted version of it is often too dark (or light) to recognise
        shown = *color;
    } else if (index != SURFACE_COLOR_INDEX) {
        shown = getDefaultColor(index);
    } else {
        // The theme's own near-black or near-white surface
        shown = isDark(ctx->pendingSettings) ? 0x131313 : 0xFAFAFA;
    }
    lv_obj_set_style_bg_color(widgets.swatch, lv_color_hex(shown), LV_STATE_DEFAULT);
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
    ctx->pendingSettings.fontSize = size == lvgl::getDefaultFontSize() ? 0 : size;
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
    lv_obj_set_scrollable(row, false);
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

/** A row with a title. Its other items move to the next line when they don't fit next to the title. */
lv_obj_t* createLabeledRow(lv_obj_t* parent, const char* text) {
    auto* row = createRow(parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    auto* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_flex_grow(label, 1);
    // The keys move between the rows, and left and right between the row's items
    lvgl_grid_navigation_add(row);
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
    widgets.row = createLabeledRow(parent, tr(COLOR_TITLES[index]));

    // The colour preview: its colour is the setting, its border uses the theme's text colour
    widgets.swatch = lv_obj_create(widgets.row);
    lv_obj_remove_style_all(widgets.swatch);
    lv_obj_set_scrollable(widgets.swatch, false);
    lv_obj_set_clickable(widgets.swatch, false);
    lv_obj_set_style_bg_opa(widgets.swatch, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(widgets.swatch, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(widgets.swatch, lv_obj_get_style_text_color(widgets.swatch, LV_PART_MAIN), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(widgets.swatch, LV_DPX(4), LV_STATE_DEFAULT);

    auto* button = lv_button_create(widgets.row);
    auto* button_icon = lv_label_create(button);
    lv_obj_set_style_text_font(button_icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(button_icon, LVGL_ICON_SHARED_PALETTE);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
    lv_obj_add_event_cb(button, onColorButtonPressed, LV_EVENT_SHORT_CLICKED, ctx);

    // As high as the button
    lv_obj_update_layout(button);
    const int32_t swatch_size = lv_obj_get_height(button);
    lv_obj_set_size(widgets.swatch, swatch_size, swatch_size);
    return widgets;
}

void createThemeCard(lv_obj_t* parent, Context* ctx) {
    auto* card = createSection(parent, tr("Theme"));

    auto* mode_row = createLabeledRow(card, tr("Mode"));
    ctx->lightChip = createChip(mode_row, tr("Light"), onLightPressed, ctx);
    ctx->darkChip = createChip(mode_row, tr("Dark"), onDarkPressed, ctx);

    // Monochrome and greyscale displays always use the mono theme, and it doesn't use colours
    if (!ctx->isMonoDisplay) {
        auto* style_row = createLabeledRow(card, tr("Style"));
        ctx->regularThemeChip = createChip(style_row, tr("Regular"), onRegularThemePressed, ctx);
        ctx->monoThemeChip = createChip(style_row, tr("Mono"), onMonoThemePressed, ctx);
    }

    // 0% is compact and 100% is regular
    lv_label_set_text(lv_label_create(card), tr("Spacing (%)"));
    ctx->spacingSlider = lvgl_sliderbox_create(card, 0, 100, settings::appearance::DENSITY_STEP, getSpacing(ctx->pendingSettings));
    lv_obj_set_width(ctx->spacingSlider, LV_PCT(100));
    lvgl_sliderbox_add_value_changed_cb(ctx->spacingSlider, onSpacingChanged, ctx);

    lv_label_set_text(lv_label_create(card), tr("Size (%)"));
    ctx->sizeSlider = lvgl_sliderbox_create(card, 0, 100, settings::appearance::DENSITY_STEP, getSize(ctx->pendingSettings));
    lv_obj_set_width(ctx->sizeSlider, LV_PCT(100));
    lvgl_sliderbox_add_value_changed_cb(ctx->sizeSlider, onSizeChanged, ctx);

    auto* animations_row = createLabeledRow(card, tr("Animations"));
    auto* animations_switch = lv_switch_create(animations_row);
    setChecked(animations_switch, ctx->pendingSettings.animationsEnabled);
    lv_obj_add_event_cb(animations_switch, onAnimationsChanged, LV_EVENT_VALUE_CHANGED, ctx);
}

void updateSystemBarsWidgets(Context* ctx, const settings::launcher::LauncherSettings& settings) {
    using settings::launcher::SystemBarsLayout;
    const bool custom = settings.systemBarsMode == settings::launcher::SystemBarsMode::Custom;
    setChecked(ctx->alignLeftChip, settings.alignment == settings::launcher::SystemBarsAlignment::Left);
    setChecked(ctx->alignRightChip, settings.alignment == settings::launcher::SystemBarsAlignment::Right);
    if (ctx->systemBarsAutoChip != nullptr) {
        setChecked(ctx->systemBarsAutoChip, !custom);
        setChecked(ctx->systemBarsCustomChip, custom);
    }
    // The layouts per orientation are only used with the Custom mode. Orientations with a fixed layout have no row.
    if (ctx->portraitRow != nullptr) {
        setHidden(ctx->portraitRow, !custom);
        setChecked(ctx->portraitSplitChip, settings.portraitLayout == SystemBarsLayout::Split);
        setChecked(ctx->portraitSideChip, settings.portraitLayout == SystemBarsLayout::Side);
    }
    if (ctx->landscapeRow != nullptr) {
        setHidden(ctx->landscapeRow, !custom);
        setChecked(ctx->landscapeSplitChip, settings.landscapeLayout == SystemBarsLayout::Split);
        setChecked(ctx->landscapeSideChip, settings.landscapeLayout == SystemBarsLayout::Side);
    }
}

// Saves the system bars setting of the pressed chip and shows the system bars with it
void onSystemBarsChipPressed(lv_event_t* event) {
    using settings::launcher::SystemBarsLayout;
    using settings::launcher::SystemBarsMode;
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_target_obj(event);
    auto settings = settings::launcher::loadOrGetDefault();
    if (chip == ctx->alignLeftChip || chip == ctx->alignRightChip) {
        settings.alignment = chip == ctx->alignRightChip ? settings::launcher::SystemBarsAlignment::Right : settings::launcher::SystemBarsAlignment::Left;
    } else if (chip == ctx->systemBarsAutoChip || chip == ctx->systemBarsCustomChip) {
        settings.systemBarsMode = chip == ctx->systemBarsCustomChip ? SystemBarsMode::Custom : SystemBarsMode::Auto;
    } else if (chip == ctx->portraitSplitChip || chip == ctx->portraitSideChip) {
        settings.portraitLayout = chip == ctx->portraitSideChip ? SystemBarsLayout::Side : SystemBarsLayout::Split;
    } else {
        settings.landscapeLayout = chip == ctx->landscapeSideChip ? SystemBarsLayout::Side : SystemBarsLayout::Split;
    }
    if (!settings::launcher::save(settings)) {
        LOG_E(TAG, "Failed to save system bars settings");
        // The chips show what is applied, which is what the file still has
        updateSystemBarsWidgets(ctx, settings::launcher::loadOrGetDefault());
        dialog::alert(ctx->appInstanceId, tr("Error"), tr("Failed to save the system bars settings"));
        return;
    }
    updateSystemBarsWidgets(ctx, settings);
    system_bars_refresh();
}

void createSystemBarsCard(lv_obj_t* parent, Context* ctx) {
    using settings::launcher::SystemBarsCapability;
    // The device's configuration can fix the layout per orientation, which leaves nothing to choose for it
    const auto capabilities = settings::launcher::getSystemBarsCapabilities();
    const bool portrait_choosable = capabilities.portrait == SystemBarsCapability::Any;
    const bool landscape_choosable = capabilities.landscape == SystemBarsCapability::Any;
    // Stale pointers from a previous build of the window must not match new widgets
    ctx->portraitRow = nullptr;
    ctx->portraitSplitChip = nullptr;
    ctx->portraitSideChip = nullptr;
    ctx->landscapeRow = nullptr;
    ctx->landscapeSplitChip = nullptr;
    ctx->landscapeSideChip = nullptr;
    ctx->systemBarsAutoChip = nullptr;
    ctx->systemBarsCustomChip = nullptr;

    auto* card = createSection(parent, tr("System Bars"));

    auto* align_row = createLabeledRow(card, tr("Align"));
    ctx->alignLeftChip = createChip(align_row, tr("Left"), onSystemBarsChipPressed, ctx);
    ctx->alignRightChip = createChip(align_row, tr("Right"), onSystemBarsChipPressed, ctx);

    if (portrait_choosable || landscape_choosable) {
        auto* mode_row = createLabeledRow(card, tr("Layout"));
        ctx->systemBarsAutoChip = createChip(mode_row, tr("Auto"), onSystemBarsChipPressed, ctx);
        ctx->systemBarsCustomChip = createChip(mode_row, tr("Custom"), onSystemBarsChipPressed, ctx);
    }

    if (portrait_choosable) {
        ctx->portraitRow = createLabeledRow(card, tr("Portrait"));
        ctx->portraitSplitChip = createChip(ctx->portraitRow, tr("Split"), onSystemBarsChipPressed, ctx);
        ctx->portraitSideChip = createChip(ctx->portraitRow, tr("Side"), onSystemBarsChipPressed, ctx);
    }

    if (landscape_choosable) {
        ctx->landscapeRow = createLabeledRow(card, tr("Landscape"));
        ctx->landscapeSplitChip = createChip(ctx->landscapeRow, tr("Split"), onSystemBarsChipPressed, ctx);
        ctx->landscapeSideChip = createChip(ctx->landscapeRow, tr("Side"), onSystemBarsChipPressed, ctx);
    }

    updateSystemBarsWidgets(ctx, settings::launcher::loadOrGetDefault());
}

void createColorsCard(lv_obj_t* parent, Context* ctx) {
    ctx->colorsTitle = lv_label_create(parent);
    lv_label_set_text(ctx->colorsTitle, tr("Theme Colors"));

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    ctx->colorsCard = card;

    for (size_t i = 0; i < COLOR_COUNT; i++) {
        ctx->colorRows[i] = createColorRow(card, i, ctx);
        if (i == SURFACE_COLOR_INDEX) {
            // How strongly the surface colour shows: it's mixed into black (dark) or white (light)
            ctx->surfaceTintRow = createLabeledRow(card, tr("Surface tint"));
            for (uint8_t level = 0; level < settings::appearance::SURFACE_TINT_LEVEL_COUNT; level++) {
                ctx->surfaceTintChips[level] = createChip(ctx->surfaceTintRow, tr(SURFACE_TINT_NAMES[level]), onSurfaceTintPressed, ctx);
                lv_obj_set_user_data(ctx->surfaceTintChips[level], reinterpret_cast<void*>(static_cast<uintptr_t>(level)));
            }
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
    // "Title                [Default] [Select]"
    // "file.ttf"
    auto* row = createRow(parent);

    // The title with the file name below it, which take the width that the buttons leave. Texts that don't fit scroll.
    auto* texts = lv_obj_create(row);
    lv_obj_remove_style_all(texts);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(texts, false);

    auto* label = lv_label_create(texts);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(label, title);

    widgets.fileLabel = lv_label_create(texts);
    lv_obj_set_width(widgets.fileLabel, LV_PCT(100));
    lv_label_set_long_mode(widgets.fileLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(widgets.fileLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    widgets.defaultButton = createButton(row, tr("Default"), onDefault, ctx);
    createButton(row, tr("Select"), onSelect, ctx);

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

    auto* toolbar = lvgl_toolbar_create(parent, tr("Appearance"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    ctx->spinner = lvgl_toolbar_add_spinner_action(toolbar);
    ctx->applyButton = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_OK, onApplyPressed, ctx);

    auto* content = lv_obj_create(parent);
    ctx->content = content;
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(content, 0, LV_STATE_DEFAULT);

    const auto color_format = lv_display_get_color_format(lv_obj_get_display(parent));
    ctx->isMonoDisplay = color_format == LV_COLOR_FORMAT_I1 || color_format == LV_COLOR_FORMAT_L8;
    createThemeCard(content, ctx);
    // Monochrome and greyscale displays always use the mono theme, which doesn't use colours
    if (!ctx->isMonoDisplay) {
        createColorsCard(content, ctx);
    }
    createSystemBarsCard(content, ctx);

    if (lvgl::isFontChangeSupported()) {
        auto* fonts_card = createSection(content, tr("Fonts"));
        auto* font_size_row = createRow(fonts_card);
        auto* font_size_label = lv_label_create(font_size_row);
        lv_label_set_text(font_size_label, tr("Font size"));
        lv_obj_set_flex_grow(font_size_label, 1);
        // [-] [size] [+]
        auto* font_size_spinbox = lv_spinbox_create(font_size_row);
        lv_spinbox_set_range(font_size_spinbox, MIN_FONT_SIZE, MAX_FONT_SIZE);
        lv_spinbox_set_digit_format(font_size_spinbox, 2, 0);
        lv_spinbox_set_step(font_size_spinbox, 1);
        // Wide enough for the digits and the room that the text field keeps for its cursor, so it never scrolls the digits out of view.
        // The size is set before the value, because setting the value scrolls to the cursor using the current size.
        const lv_font_t* font_size_font = lv_obj_get_style_text_font(font_size_spinbox, LV_PART_MAIN);
        lv_point_t digits_size;
        lv_text_get_size(&digits_size, "00", font_size_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const int32_t font_size_content_width = digits_size.x + lv_font_get_line_height(font_size_font);
        lv_obj_set_width(font_size_spinbox, font_size_content_width +
            lv_obj_get_style_pad_left(font_size_spinbox, LV_PART_MAIN) + lv_obj_get_style_pad_right(font_size_spinbox, LV_PART_MAIN) +
            2 * lv_obj_get_style_border_width(font_size_spinbox, LV_PART_MAIN));
        lv_obj_set_style_text_align(font_size_spinbox, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        const uint16_t font_size = ctx->pendingSettings.fontSize != 0 ? ctx->pendingSettings.fontSize : lvgl::getDefaultFontSize();
        lv_spinbox_set_value(font_size_spinbox, font_size);
        // The value is changed with the buttons, so the digit cursor isn't shown
        lv_obj_set_style_bg_opa(font_size_spinbox, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_add_event_cb(font_size_spinbox, onFontSizeChanged, LV_EVENT_VALUE_CHANGED, ctx);
        auto* decrement_button = createStepButton(font_size_row, LV_SYMBOL_MINUS, onFontSizeDecrementPressed, font_size_spinbox);
        lv_obj_move_to_index(decrement_button, lv_obj_get_index(font_size_spinbox));
        createStepButton(font_size_row, LV_SYMBOL_PLUS, onFontSizeIncrementPressed, font_size_spinbox);

        ctx->regularRow = createFontRow(fonts_card, tr("Regular font"), onSelectRegularPressed, onDefaultRegularPressed, ctx);
        ctx->monoRow = createFontRow(fonts_card, tr("Monospace font"), onSelectMonoPressed, onDefaultMonoPressed, ctx);

        ctx->errorLabel = lv_label_create(fonts_card);
        lv_obj_set_width(ctx->errorLabel, LV_PCT(100));
        lv_label_set_long_mode(ctx->errorLabel, LV_LABEL_LONG_MODE_WRAP);
        lv_label_set_text(ctx->errorLabel, tr("Failed to create the fonts. Check that the selected files are TrueType fonts."));
        lv_obj_set_style_text_color(ctx->errorLabel, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);
    }

    updateWidgets(ctx);

    // The window is rebuilt when it's shown again, e.g. after a selection dialog
    lv_obj_update_layout(content);
    lv_obj_scroll_to_y(content, ctx->contentScrollY, LV_ANIM_OFF);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    // The LVGL lock is already held, so the content can still be read
    ctx->contentScrollY = lv_obj_get_scroll_y(ctx->content);
    ctx->content = nullptr;
    ctx->applyButton = nullptr;
    ctx->spinner = nullptr;
    ctx->regularRow = {};
    ctx->monoRow = {};
    ctx->errorLabel = nullptr;
    ctx->lightChip = nullptr;
    ctx->darkChip = nullptr;
    ctx->spacingSlider = nullptr;
    ctx->sizeSlider = nullptr;
    ctx->regularThemeChip = nullptr;
    ctx->monoThemeChip = nullptr;
    ctx->colorsTitle = nullptr;
    ctx->colorsCard = nullptr;
    for (auto& row : ctx->colorRows) {
        row = {};
    }
    ctx->surfaceTintRow = nullptr;
    for (auto& chip : ctx->surfaceTintChips) {
        chip = nullptr;
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
    if (ctx->applying) {
        return;
    }
    // Fails while another dialog is shown, whose slot then stays
    const auto previous_slot = ctx->selectSlot;
    ctx->selectSlot = slot;
    const error_t error = window_manager_file_dialog(ctx->appInstanceId, nullptr, [](const char* const* paths, uint32_t path_count, void* userData) {
        auto* ctx = static_cast<Context*>(userData);
        ctx->selectedPath = path_count > 0 ? paths[0] : "";
        task_event_group_signal(ctx->eventGroup, ctx->fileResultBit);
    }, ctx);
    if (error != ERROR_NONE) {
        ctx->selectSlot = previous_slot;
    }
}

void startColorSelection(Context* ctx) {
    if (ctx->applying) {
        return;
    }
    std::vector<std::string> items;
    items.reserve(PALETTE_COLOR_COUNT + 1);
    items.emplace_back(tr("Default"));
    for (const auto& named_color : PALETTE_COLORS) {
        items.emplace_back(tr(named_color.name));
    }
    dialog::selection(ctx->appInstanceId, tr(COLOR_TITLES[ctx->selectColorIndex]), items, [](int32_t selected, void* userData) {
        auto* ctx = static_cast<Context*>(userData);
        ctx->colorResult = selected;
        task_event_group_signal(ctx->eventGroup, ctx->colorResultBit);
    }, ctx);
}

void onColorSelectionResult(Context* ctx) {
    // Index 0 is "Default", the others are the palette colours. Other results mean that the dialog was dismissed.
    const int32_t selected = ctx->colorResult;
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
}

void onFileSelectionResult(Context* ctx) {
    const auto selected_path = ctx->selectedPath;
    if (selected_path.empty()) {
        return;
    }
    const size_t character_count = getCharacterCount(selected_path);
    lvgl_lock();
    getFontPath(ctx->pendingSettings, ctx->selectSlot) = selected_path;
    getCharacterCount(ctx, ctx->selectSlot) = character_count;
    updateWidgets(ctx);
    lvgl_unlock();
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
    check(task_event_group_claim_bit(&event_group, &ctx.colorResultBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.fileResultBit) == ERROR_NONE);
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
        if (flags & ctx.colorResultBit) {
            onColorSelectionResult(&ctx);
        }
        if (flags & ctx.fileResultBit) {
            onFileSelectionResult(&ctx);
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
