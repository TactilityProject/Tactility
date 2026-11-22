#define L10N_APP "systeminfo"
#include <Tactility/i18n/AppDomain.h>

#include "tactility/time.h"
#include <tactility/check.h>
#include <tactility/memory.h>

#include <Tactility/DeprecatedPaths.h>
#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <algorithm>
#include <strings.h>
#include <format>
#include <utility>
#include <vector>
#include <string>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/tabs.h>

#include <lvgl_window_manager/window_manager.h>

#ifdef ESP_PLATFORM
#include <esp_vfs_fat.h>
#include <esp_heap_caps.h>
#endif

namespace tt::app::systeminfo {

constexpr auto* TAG = "SystemInfo";

extern const ::AppManifest manifest;

namespace {

size_t getHeapFree() {
    return memory_heap_free();
}

size_t getHeapTotal() {
    return memory_heap_total();
}

size_t getSpiFree() {
    return memory_external_free();
}

size_t getHeapLargestBlock() {
#ifdef ESP_PLATFORM
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
#else
    // Not known on the simulator, so the card leaves it out
    return 0;
#endif
}

size_t getSpiLargestBlock() {
#ifdef ESP_PLATFORM
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#else
    // Not known on the simulator, so the card leaves it out
    return 0;
#endif
}

size_t getSpiTotal() {
    return memory_external_total();
}

enum class StorageUnit {
    Bytes,
    Kilobytes,
    Megabytes,
    Gigabytes
};

StorageUnit getStorageUnit(uint64_t value) {
    using enum StorageUnit;
    if (value / (1024 * 1024 * 1024) > 0) {
        return Gigabytes;
    } else if (value / (1024 * 1024) > 0) {
        return Megabytes;
    } else if (value / 1024 > 0) {
        return Kilobytes;
    } else {
        return Bytes;
    }
}

std::string getStorageUnitString(StorageUnit unit) {
    using enum StorageUnit;
    switch (unit) {
        case Bytes:
            return "bytes";
        case Kilobytes:
            return "kB";
        case Megabytes:
            return "MB";
        case Gigabytes:
            return "GB";
        default:
            std::unreachable();
    }
}

std::string getStorageValue(StorageUnit unit, uint64_t bytes) {
    using enum StorageUnit;
    switch (unit) {
        case Bytes:
            return std::to_string(bytes);
        case Kilobytes:
            return std::to_string(bytes / 1024);
        case Megabytes:
            return std::format("{:.1f}", static_cast<float>(bytes) / 1024.f / 1024.f);
        case Gigabytes:
            return std::format("{:.1f}", static_cast<float>(bytes) / 1024.f / 1024.f / 1024.f);
        default:
            std::unreachable();
    }
}


/** A card for a memory pool or a storage volume: its name, the share in use, a bar and a line with the free space */
struct UsageCardWidgets {
    lv_obj_t* card = nullptr;
    lv_obj_t* texts = nullptr;
    /** A ring that shows the share in use, like the memory gauges of the system menu */
    lv_obj_t* arc = nullptr;
    lv_obj_t* detail = nullptr;
    /** The detail line consists of these two, and the second one (empty for storage) is left out when they don't fit together */
    std::string freeText;
    std::string largestBlockText;
};

int32_t getTextWidth(lv_obj_t* label, const std::string& text) {
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN), lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/** Leaves out the largest block when the line doesn't fit, and ends in an ellipsis when the free memory doesn't fit either */
void fitUsageCardDetail(UsageCardWidgets& widgets) {
    const std::string full = widgets.freeText + widgets.largestBlockText;
    const bool fits = getTextWidth(widgets.detail, full) <= lv_obj_get_content_width(widgets.texts);
    lv_label_set_text(widgets.detail, fits ? full.c_str() : widgets.freeText.c_str());
}

void onUsageCardSizeChanged(lv_event_t* event) {
    fitUsageCardDetail(*static_cast<UsageCardWidgets*>(lv_event_get_user_data(event)));
}

/** @param[in] widgets keeps the card's widgets, and must outlive the card */
void createUsageCard(lv_obj_t* parent, const char* name, UsageCardWidgets& widgets) {
    widgets.card = lvgl_card_create(parent);
    lv_obj_set_size(widgets.card, LV_PCT(100), LV_SIZE_CONTENT);
    // The name and the free space left of the ring
    lv_obj_set_flex_flow(widgets.card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(widgets.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(widgets.card, LV_DPX(6), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(widgets.card, false);
    // Selectable, so the keys can scroll to it on small displays
    lv_obj_set_clickable(widgets.card, true);
    lv_obj_set_click_focusable(widgets.card, true);

    widgets.texts = lv_obj_create(widgets.card);
    lv_obj_remove_style_all(widgets.texts);
    lv_obj_set_size(widgets.texts, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(widgets.texts, 1);
    lv_obj_set_flex_flow(widgets.texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(widgets.texts, false);

    auto* nameLabel = lv_label_create(widgets.texts);
    lv_label_set_text(nameLabel, name);
    // One line that ends in an ellipsis, as it would wrap otherwise
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(nameLabel, LV_PCT(100), lv_font_get_line_height(lv_obj_get_style_text_font(nameLabel, LV_PART_MAIN)));

    widgets.detail = lv_label_create(widgets.texts);
    const lv_font_t* smallFont = lvgl_get_text_font(FONT_SIZE_SMALL);
    lv_obj_set_style_text_font(widgets.detail, smallFont, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widgets.detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(widgets.detail, LV_PCT(100), lv_font_get_line_height(smallFont));

    // The same ring as the memory gauges of the system menu, as tall as the two lines of text
    const int32_t size = lv_font_get_line_height(smallFont) * 2 + LV_DPX(10);
    widgets.arc = lv_arc_create(widgets.card);
    lv_obj_set_size(widgets.arc, size, size);
    lv_arc_set_rotation(widgets.arc, 270);
    lv_arc_set_bg_angles(widgets.arc, 0, 360);
    lv_arc_set_range(widgets.arc, 0, 100);
    lv_obj_remove_style(widgets.arc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(widgets.arc, LV_DPX(5), LV_PART_MAIN);
    lv_obj_set_style_arc_width(widgets.arc, LV_DPX(5), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(widgets.arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(widgets.arc, 0, LV_STATE_DEFAULT);
    if (lvgl_theme_is_mono()) {
        // A thin ring over the outer edge of the arc, drawn after it
        lv_obj_set_style_bg_opa(widgets.arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_radius(widgets.arc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(widgets.arc, LV_DPX(1), LV_PART_MAIN);
        lv_obj_set_style_border_opa(widgets.arc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_post(widgets.arc, true, LV_PART_MAIN);
    }
    lv_obj_set_clickable(widgets.arc, false);
    // The colours of the system menu's gauges: the unfilled part is the card's background, a little towards the text colour
    const lv_color_t textColor = lv_obj_get_style_text_color(widgets.card, LV_PART_MAIN);
    const lv_color_t cardColor = lv_obj_get_style_bg_color(widgets.card, LV_PART_MAIN);
    if (lvgl_theme_is_mono()) {
        // Only the ring marks the unfilled part, as a gray can't be shown
        lv_obj_set_style_arc_color(widgets.arc, textColor, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(widgets.arc, cardColor, LV_PART_MAIN);
        lv_obj_set_style_border_color(widgets.arc, textColor, LV_PART_MAIN);
    } else {
        LvglThemeSettings theme {};
        lvgl_theme_get_settings(&theme);
        lv_obj_set_style_arc_color(widgets.arc, lv_color_mix(textColor, cardColor, 40), LV_PART_MAIN);
        lv_obj_set_style_arc_color(widgets.arc, theme.color_secondary, LV_PART_INDICATOR);
    }

    lv_obj_add_event_cb(widgets.card, onUsageCardSizeChanged, LV_EVENT_SIZE_CHANGED, &widgets);
}

std::string formatSize(uint64_t bytes) {
    const auto unit = getStorageUnit(bytes);
    return getStorageValue(unit, bytes) + " " + getStorageUnitString(unit);
}

/** @param[in] largestBlock the largest free block of a memory pool, or 0 for storage */
void updateUsageCard(UsageCardWidgets& widgets, uint64_t free, uint64_t total, uint64_t largestBlock) {
    // Not created, e.g. an SD card that was inserted after the widgets were created
    if (widgets.card == nullptr) {
        return;
    }
    // The ring shows the free share, like the memory gauges of the system menu
    const int32_t percent = (total > 0) ? static_cast<int32_t>((free * 100 + total / 2) / total) : 0;
    lv_arc_set_value(widgets.arc, percent);
    widgets.freeText = i18n::format(tr("{} free of {}"), { formatSize(free), formatSize(total) });
    widgets.largestBlockText = (largestBlock > 0) ? i18n::format(tr(" · largest block {}"), { formatSize(largestBlock) }) : "";
    fitUsageCardDetail(widgets);
}



#if configUSE_TRACE_FACILITY

const char* getTaskState(const TaskStatus_t& task) {
    switch (task.eCurrentState) {
        case eRunning:
            return tr("running");
        case eReady:
            return tr("ready");
        case eBlocked:
            return tr("blocked");
        case eSuspended:
            return tr("suspended");
        case eDeleted:
            return tr("deleted");
        case eInvalid:
        default:
            return tr("invalid");
    }
}

const char* getTaskName(const TaskStatus_t& task) {
    return (task.pcTaskName == nullptr || task.pcTaskName[0] == 0) ? tr("(unnamed)") : task.pcTaskName;
}

/**
 * Updates the rows in place, so the list keeps its height and scroll position, and the selected task stays selected.
 * @param[in] parent the list
 * @param[in,out] taskNames the task name of each row
 */
void updateRtosTasks(lv_obj_t* parent, std::vector<std::string>& taskNames) {
    lv_obj_t* selected = lvgl_grid_navigation_get_focused(parent);
    const int32_t selected_index = selected != nullptr ? lv_obj_get_index(selected) : -1;
    const std::string selected_name = selected_index >= 0 && selected_index < static_cast<int32_t>(taskNames.size()) ? taskNames[selected_index] : "";
    const bool show_selection = selected != nullptr && lv_obj_has_state(selected, LV_STATE_FOCUS_KEY);

    UBaseType_t count = uxTaskGetNumberOfTasks();
    auto* tasks = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * count);
    if (!tasks) {
        lv_obj_clean(parent);
        taskNames.clear();
        auto* error_label = lv_label_create(parent);
        lv_label_set_text(error_label, tr("Failed to allocate memory for task list"));
        return;
    }
    uint32_t totalRuntime = 0;
    UBaseType_t actual = uxTaskGetSystemState(tasks, count, &totalRuntime);
    std::sort(tasks, tasks + actual, [](const TaskStatus_t& a, const TaskStatus_t& b) {
        return strcasecmp(getTaskName(a), getTaskName(b)) < 0;
    });

    taskNames.clear();
    for (UBaseType_t i = 0; i < actual; ++i) {
        auto* row = i < lv_obj_get_child_count(parent) ? lv_obj_get_child(parent, static_cast<int32_t>(i)) : lv_label_create(parent);
        lv_label_set_text_fmt(row, "%s (%s)", getTaskName(tasks[i]), getTaskState(tasks[i]));
        // Keys and encoders move through the tasks, which scrolls the list
        lv_obj_set_clickable(row, true);
        taskNames.emplace_back(getTaskName(tasks[i]));
    }
    free(tasks);
    while (lv_obj_get_child_count(parent) > actual) {
        lv_obj_delete(lv_obj_get_child(parent, -1));
    }

    if (selected_index < 0 || taskNames.empty()) {
        return;
    }
    // Follows the task, or keeps the position when the task is gone
    auto it = std::ranges::find(taskNames, selected_name);
    const auto index = it != taskNames.end() ? static_cast<int32_t>(it - taskNames.begin()) : std::min(selected_index, static_cast<int32_t>(taskNames.size()) - 1);
    // Also re-applied when the row didn't change: deleting rows resets grid navigation's selection
    auto* row = lv_obj_get_child(parent, index);
    lv_gridnav_set_focused(parent, row, LV_ANIM_OFF);
    const bool list_focused = lv_obj_get_group(parent) != nullptr && lv_group_get_focused(lv_obj_get_group(parent)) == parent;
    if (!show_selection) {
        lv_obj_remove_state(row, LV_STATE_FOCUS_KEY);
    }
    if (list_focused) {
        lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
    } else {
        lv_obj_remove_state(row, LV_STATE_FOCUSED);
    }
}

#endif

/** The content of a tab: a scrollable column that fills the content area, shown while its tab is selected */
lv_obj_t* createPane(lv_obj_t* parent) {
    auto* pane = lv_obj_create(parent);
    lv_obj_set_size(pane, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pane, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pane, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(pane, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    return pane;
}

/** In the order of the tabs */
enum class Tab { Memory, Storage, Tasks, About };
constexpr size_t TAB_COUNT = 4;

struct Context {
    uint32_t appInstanceId;

    std::unique_ptr<Timer> memoryTimer;
    std::unique_ptr<Timer> tasksTimer;

    UsageCardWidgets internalMemCard;
    UsageCardWidgets externalMemCard;
    UsageCardWidgets dataStorageCard;
    UsageCardWidgets sdcardStorageCard;

    lv_obj_t* tasksContainer = nullptr;
    /** The task name of each row in tasksContainer */
    std::vector<std::string> taskNames;
    lv_obj_t* psramContainer = nullptr;
    lv_obj_t* tabs = nullptr;
    lv_obj_t* panes[TAB_COUNT] = {};
    /** Survives a rebuild of the widgets */
    uint32_t selectedTab = 0;

    bool hasExternalMem = false;
    bool hasDataStorage = false;
};


void updateMemory(Context* ctx) {
    updateUsageCard(ctx->internalMemCard, getHeapFree(), getHeapTotal(), getHeapLargestBlock());

    if (ctx->hasExternalMem) {
        updateUsageCard(ctx->externalMemCard, getSpiFree(), getSpiTotal(), getSpiLargestBlock());
    }
}

void updateStorage(Context* ctx) {
#ifdef ESP_PLATFORM
    uint64_t storage_total = 0;
    uint64_t storage_free = 0;

    if (ctx->hasDataStorage) {
        if (esp_vfs_fat_info("/data", &storage_total, &storage_free) == ESP_OK) {
            updateUsageCard(ctx->dataStorageCard, storage_free, storage_total, 0);
        }
    }

    std::string sdcard_path;
    if (findFirstMountedSdCardPath(sdcard_path) && esp_vfs_fat_info(sdcard_path.c_str(), &storage_total, &storage_free) == ESP_OK) {
        updateUsageCard(ctx->sdcardStorageCard, storage_free, storage_total, 0);
    }

#endif
}

void updateTasks(Context* ctx) {
#if configUSE_TRACE_FACILITY
    if (ctx->tasksContainer) {
        updateRtosTasks(ctx->tasksContainer, ctx->taskNames);  // Tasks tab: show state
    }
#endif
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Only the focusable content of the shown tab is in the focus group */
void setFocusable(lv_group_t* group, lv_obj_t* container, bool shown) {
    if (container == nullptr) {
        return;
    }
    if (shown && lv_obj_get_group(container) == nullptr) {
        lv_group_add_obj(group, container);
    } else if (!shown && lv_obj_get_group(container) != nullptr) {
        lv_group_remove_obj(container);
    }
}

/** Shows the content of the selected tab. Only the shown tab's cards or task list can be focused. */
void showSelectedTab(Context* ctx) {
    for (size_t i = 0; i < TAB_COUNT; i++) {
        lv_obj_set_hidden(ctx->panes[i], i != ctx->selectedTab);
    }
    auto* group = lv_group_get_default();
    if (group == nullptr) {
        return;
    }
    for (const Tab tab : { Tab::Memory, Tab::Storage }) {
        setFocusable(group, ctx->panes[static_cast<size_t>(tab)], ctx->selectedTab == static_cast<uint32_t>(tab));
    }
#if configUSE_TRACE_FACILITY
    setFocusable(group, ctx->tasksContainer, ctx->selectedTab == static_cast<uint32_t>(Tab::Tasks));
#endif
}

void onTabChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->selectedTab = lvgl_tabs_get_selected(ctx->tabs);
    showSelectedTab(ctx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    // Replaces the toolbar: the close button and the tabs next to it.
    // The close button and the tabs are grid navigation groups of their own, as grid navigation can't be nested.
    auto* header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const auto spacing = lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing());
    lv_obj_set_style_pad_hor(header, spacing, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(header, spacing, LV_STATE_DEFAULT);

    // A plain button ignores left and right, so it's a grid navigation group of its own to reach the tabs with the keys
    auto* close = lv_obj_create(header);
    lv_obj_remove_style_all(close);
    lv_obj_set_size(close, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(close, LV_FLEX_FLOW_ROW);
    auto* closeButton = lvgl_icon_button_create(close);
    lv_obj_add_event_cb(closeButton, onBackPressed, LV_EVENT_SHORT_CLICKED, ctx);
    auto* closeIcon = lv_label_create(closeButton);
    lv_obj_set_style_text_font(closeIcon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(closeIcon, LVGL_ICON_SHARED_CLOSE);
    lvgl_grid_navigation_add(close);
    lvgl_grid_navigation_focus_without_touch(close, closeButton);

    // Tabs that don't fit show only their icon
    ctx->tabs = lvgl_tabs_create(header);
    lvgl_tabs_set_mode(ctx->tabs, LVGL_TABS_MODE_WRAP);
    lvgl_tabs_add_tab_with_icon(ctx->tabs, LVGL_ICON_SHARED_MEMORY, tr("Memory"));
    lvgl_tabs_add_tab_with_icon(ctx->tabs, LVGL_ICON_SHARED_HARD_DISK, tr("Storage"));
    lvgl_tabs_add_tab_with_icon(ctx->tabs, LVGL_ICON_SHARED_SELECT_WINDOW_2, tr("Tasks"));
    lvgl_tabs_add_tab_with_icon(ctx->tabs, LVGL_ICON_SHARED_INFO, tr("About"));
    lvgl_tabs_set_selected(ctx->tabs, ctx->selectedTab);
    lv_obj_add_event_cb(ctx->tabs, onTabChanged, LV_EVENT_VALUE_CHANGED, ctx);

    auto* content = lv_obj_create(parent);
    lv_obj_set_style_border_width(content, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(content, false);
    for (auto*& pane : ctx->panes) {
        pane = createPane(content);
    }
    auto* memory_tab = ctx->panes[static_cast<size_t>(Tab::Memory)];
    auto* storage_tab = ctx->panes[static_cast<size_t>(Tab::Storage)];
    auto* tasks_tab = ctx->panes[static_cast<size_t>(Tab::Tasks)];
    auto* about_tab = ctx->panes[static_cast<size_t>(Tab::About)];
    // Space between the cards
    for (auto* pane : { memory_tab, storage_tab }) {
        lv_obj_set_style_pad_row(pane, lvgl_theme_lerp(LV_DPX(4), LV_DPX(8), lvgl_theme_get_spacing()), LV_STATE_DEFAULT);
        lvgl_grid_navigation_add(pane);
    }

    // Memory tab content
    createUsageCard(memory_tab, tr("Internal RAM"), ctx->internalMemCard);

    ctx->hasExternalMem = getSpiTotal() > 0;
    if (ctx->hasExternalMem) {
        createUsageCard(memory_tab, tr("PSRAM"), ctx->externalMemCard);
    }

#ifdef ESP_PLATFORM
    // Storage tab content
    uint64_t storage_total = 0;
    uint64_t storage_free = 0;

    ctx->hasDataStorage = (esp_vfs_fat_info("/data", &storage_total, &storage_free) == ESP_OK);
    if (ctx->hasDataStorage) {
        createUsageCard(storage_tab, tr("Internal storage"), ctx->dataStorageCard);
    }

    std::string sdcard_path;
    if (findFirstMountedSdCardPath(sdcard_path) && esp_vfs_fat_info(sdcard_path.c_str(), &storage_total, &storage_free) == ESP_OK) {
        createUsageCard(storage_tab, tr("SD card"), ctx->sdcardStorageCard);
    }
#endif

#if configUSE_TRACE_FACILITY
    // Tasks tab - container for dynamic updates
    ctx->tasksContainer = lv_obj_create(tasks_tab);
    lv_obj_set_size(ctx->tasksContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ctx->tasksContainer, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->tasksContainer, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ctx->tasksContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(ctx->tasksContainer, 0, LV_STATE_DEFAULT);
    lvgl_grid_navigation_add(ctx->tasksContainer);
#endif
    showSelectedTab(ctx);

    // Build info
    auto* tactility_version = lv_label_create(about_tab);
    lv_label_set_text_fmt(tactility_version, "Tactility v%s", TT_VERSION);
#ifdef ESP_PLATFORM
    auto* esp_idf_version = lv_label_create(about_tab);
    lv_label_set_text_fmt(esp_idf_version, "ESP-IDF v%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
#endif

    auto* device_vendor = lv_label_create(about_tab);
    lv_label_set_text(device_vendor, i18n::format(tr("Hardware vendor: {}"), { CONFIG_TT_DEVICE_VENDOR }).c_str());
    auto* device_device_name = lv_label_create(about_tab);
    lv_label_set_text(device_device_name, i18n::format(tr("Hardware model: {}"), { CONFIG_TT_DEVICE_NAME_SIMPLE }).c_str());

    // Initial updates
    updateMemory(ctx);
    updateStorage(ctx);  // Storage: one-time update on show (doesn't change frequently)
    updateTasks(ctx);
}

/** The widgets are deleted when the window is buried, and the timers keep running */
void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->internalMemCard = UsageCardWidgets {};
    ctx->externalMemCard = UsageCardWidgets {};
    ctx->dataStorageCard = UsageCardWidgets {};
    ctx->sdcardStorageCard = UsageCardWidgets {};
    ctx->tasksContainer = nullptr;
    ctx->taskNames.clear();
    ctx->tabs = nullptr;
    for (auto*& pane : ctx->panes) {
        pane = nullptr;
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    // Run for this app instance's whole lifetime (mirrors GpsSettings) - both timers keep the
    // displayed values fresh regardless of whether the app is currently topmost.
    ctx.memoryTimer = std::make_unique<Timer>(Timer::Type::Periodic, millis_to_ticks(10000), [&ctx] {
        lvgl_lock();
        updateMemory(&ctx);
        lvgl_unlock();
    });

    ctx.tasksTimer = std::make_unique<Timer>(Timer::Type::Periodic, millis_to_ticks(15000), [&ctx] {
        lvgl_lock();
        updateTasks(&ctx);
        lvgl_unlock();
    });

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);
    ctx.memoryTimer->start();   // Memory: every 10s
    ctx.tasksTimer->start();    // Tasks/CPU: every 15s

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    ctx.memoryTimer->stop();
    ctx.tasksTimer->stop();
    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.systeminfo",
    .name = "System Info",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {},
};

} // namespace
