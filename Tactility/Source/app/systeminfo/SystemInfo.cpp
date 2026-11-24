#define L10N_APP "systeminfo"
#include <Tactility/i18n/AppDomain.h>

#include "tactility/time.h"
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/memory.h>
#include <tactility/paths.h>

#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <algorithm>
#include <strings.h>
#include <format>
#include <memory>
#include <utility>
#include <cctype>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/tabs.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#ifdef ESP_PLATFORM
#include <esp_vfs_fat.h>
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>
#else
#include <sys/statvfs.h>
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

tt::std::string getStorageUnitString(StorageUnit unit) {
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
            ::std::unreachable();
    }
}

tt::std::string getStorageValue(StorageUnit unit, uint64_t bytes) {
    using enum StorageUnit;
    switch (unit) {
        case Bytes:
            return ::std::to_string(bytes).c_str();
        case Kilobytes:
            return ::std::to_string(bytes / 1024).c_str();
        case Megabytes:
            return ::std::format("{:.1f}", static_cast<float>(bytes) / 1024.f / 1024.f).c_str();
        case Gigabytes:
            return ::std::format("{:.1f}", static_cast<float>(bytes) / 1024.f / 1024.f / 1024.f).c_str();
        default:
            ::std::unreachable();
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
    tt::std::string freeText;
    tt::std::string largestBlockText;
};

int32_t getTextWidth(lv_obj_t* label, const tt::std::string& text) {
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN), lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/** Leaves out the largest block when the line doesn't fit, and ends in an ellipsis when the free memory doesn't fit either */
void fitUsageCardDetail(UsageCardWidgets& widgets) {
    const tt::std::string full = widgets.freeText + widgets.largestBlockText;
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

tt::std::string formatSize(uint64_t bytes) {
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

/** The states of FreeRTOS as a user sees them */
enum class TaskGroup { Running, Ready, Waiting, Paused };

/** The first chip shows all tasks, and the others one group each */
constexpr size_t TASK_CHIP_COUNT = 1 + 4;
/** Tasks with less stack than this left at their worst are listed first */
constexpr uint32_t LOW_STACK_BYTES = 512;

struct TaskInfo {
    tt::std::string name;
    TaskGroup group;
    UBaseType_t priority;
    /** The least stack that the task has had left */
    uint32_t stackFree;
};

TaskGroup getTaskGroup(eTaskState state) {
    switch (state) {
        case eRunning:
            return TaskGroup::Running;
        case eReady:
            return TaskGroup::Ready;
        case eSuspended:
            return TaskGroup::Paused;
        case eBlocked:
        case eDeleted:
        case eInvalid:
        default:
            return TaskGroup::Waiting;
    }
}

/** Each state has its own shape, so it can be told apart without colour */
const char* getGroupIcon(TaskGroup group) {
    switch (group) {
        case TaskGroup::Running:
            return LVGL_ICON_SHARED_PLAY_ARROW;
        case TaskGroup::Ready:
            return LVGL_ICON_SHARED_RADIO_BUTTON_UNCHECKED;
        case TaskGroup::Paused:
            return LVGL_ICON_SHARED_PAUSE;
        case TaskGroup::Waiting:
        default:
            return LVGL_ICON_SHARED_HOURGLASS;
    }
}

const char* getGroupWord(TaskGroup group) {
    switch (group) {
        case TaskGroup::Running:
            return tr("Running");
        case TaskGroup::Ready:
            return tr("Ready");
        case TaskGroup::Paused:
            return tr("Paused");
        case TaskGroup::Waiting:
        default:
            return tr("Waiting");
    }
}

const char* getGroupDescription(size_t filter) {
    switch (filter) {
        case 1:
            return tr("Running now");
        case 2:
            return tr("Ready to run");
        case 3:
            return tr("Waiting for something");
        case 4:
            return tr("Paused tasks");
        default:
            return tr("Low stack first");
    }
}

/** The colour supports the shape, and the monochrome theme has none */
void setGroupColor(lv_obj_t* obj, TaskGroup group) {
    if (lvgl_theme_is_mono()) {
        return;
    }
    lv_palette_t palette = LV_PALETTE_GREY;
    switch (group) {
        case TaskGroup::Running:
            palette = LV_PALETTE_GREEN;
            break;
        case TaskGroup::Ready:
            palette = LV_PALETTE_BLUE;
            break;
        case TaskGroup::Paused:
            palette = LV_PALETTE_ORANGE;
            break;
        case TaskGroup::Waiting:
            break;
    }
    lv_obj_set_style_text_color(obj, lv_palette_main(palette), LV_STATE_DEFAULT);
}

const char* getTaskName(const TaskStatus_t& task) {
    return (task.pcTaskName == nullptr || task.pcTaskName[0] == 0) ? tr("(unnamed)") : task.pcTaskName;
}

/** The tasks with a low stack first, then the paused ones, then the rest by name */
tt::std::vector<TaskInfo> collectTasks() {
    tt::std::vector<TaskInfo> result;
    const UBaseType_t count = uxTaskGetNumberOfTasks();
    auto* tasks = static_cast<TaskStatus_t*>(malloc(sizeof(TaskStatus_t) * count));
    if (tasks == nullptr) {
        return result;
    }
    uint32_t totalRuntime = 0;
    const UBaseType_t actual = uxTaskGetSystemState(tasks, count, &totalRuntime);
    for (UBaseType_t i = 0; i < actual; i++) {
        // A finished task is listed until the idle task has freed it
        if (tasks[i].eCurrentState == eDeleted) {
            continue;
        }
        result.push_back({
            getTaskName(tasks[i]),
            getTaskGroup(tasks[i].eCurrentState),
            tasks[i].uxCurrentPriority,
            static_cast<uint32_t>(tasks[i].usStackHighWaterMark * sizeof(StackType_t))
        });
    }
    free(tasks);
    ::std::sort(result.begin(), result.end(), [](const TaskInfo& a, const TaskInfo& b) {
        const bool lowA = a.stackFree < LOW_STACK_BYTES;
        const bool lowB = b.stackFree < LOW_STACK_BYTES;
        if (lowA != lowB) {
            return lowA;
        }
        const bool pausedA = a.group == TaskGroup::Paused;
        const bool pausedB = b.group == TaskGroup::Paused;
        if (pausedA != pausedB) {
            return pausedA;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return result;
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
/** A mounted file system, which gets a card in the storage tab */
struct StorageVolume {
    tt::std::string name;
    tt::std::string path;
    /** The capacity when the app started */
    uint64_t total = 0;
    uint64_t free = 0;
    UsageCardWidgets card;
};

bool getStorageUsage(const char* path, uint64_t& total, uint64_t& free) {
#ifdef ESP_PLATFORM
    return esp_vfs_fat_info(path, &total, &free) == ESP_OK;
#else
    struct statvfs stats {};
    if (statvfs(path, &stats) != 0) {
        return false;
    }
    total = static_cast<uint64_t>(stats.f_blocks) * stats.f_frsize;
    free = static_cast<uint64_t>(stats.f_bavail) * stats.f_frsize;
    return true;
#endif
}

/** Collects the mounted file systems of which the capacity is known. Querying the capacity can be slow, so this isn't for the LVGL task. */
bool collectStorageVolume(FileSystem* fs, void* context) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    uint64_t total = 0;
    uint64_t free = 0;
    if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE || !getStorageUsage(path, total, free)) {
        return true;
    }
    auto* owner = file_system_get_owner(fs);
    const bool sdcard = owner != nullptr && device_get_type(owner) == &SDCARD_TYPE;
    static_cast<tt::std::vector<StorageVolume>*>(context)->push_back({ sdcard ? tr("SD card") : file_system_get_name(fs), path, total, free, {} });
    return true;
}

#ifdef ESP_PLATFORM
/** "ESP32S3" as "ESP32-S3" */
tt::std::string getChipName() {
    tt::std::string name = CONFIG_IDF_TARGET;
    ::std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(::std::toupper(c)); });
    if (name.size() > 5 && name.starts_with("ESP32")) {
        name.insert(5, "-");
    }
    return name;
}
#endif

tt::std::string formatUptime(uint64_t seconds) {
    const uint64_t minutes = seconds / 60;
    const uint64_t hours = minutes / 60;
    const uint64_t days = hours / 24;
    if (days > 0) {
        return i18n::format(tr("{} d {} h"), { ::std::to_string(days), ::std::to_string(hours % 24) }).c_str();
    }
    if (hours > 0) {
        return i18n::format(tr("{} h {} min"), { ::std::to_string(hours), ::std::to_string(minutes % 60) }).c_str();
    }
    return i18n::format(tr("{} min"), { ::std::to_string(minutes) }).c_str();
}

/** A card of rows with a name on the left and the value on the right. It can be selected, so the keys can scroll to it on small displays. */
lv_obj_t* createInfoCard(lv_obj_t* parent, const char* title) {
    auto* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(titleLabel, LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(titleLabel, LV_DPX(8), LV_STATE_DEFAULT);

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, LV_DPX(6), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);
    lv_obj_set_click_focusable(card, true);
    return card;
}

/** @return the label with the value */
lv_obj_t* addInfoRow(lv_obj_t* card, const char* name, const char* value) {
    auto* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, LV_DPX(12), LV_STATE_DEFAULT);
    lv_obj_set_clickable(row, false);
    lv_obj_set_scrollable(row, false);

    auto* nameLabel = lv_label_create(row);
    lv_label_set_text(nameLabel, name);
    lv_obj_set_style_text_opa(nameLabel, LV_OPA_70, LV_STATE_DEFAULT);

    auto* valueLabel = lv_label_create(row);
    lv_obj_set_flex_grow(valueLabel, 1);
    lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
    lv_label_set_text(valueLabel, value);
    return valueLabel;
}

enum class Tab { Memory, Storage, Tasks, About };
constexpr size_t TAB_COUNT = 4;

struct Context {
    uint32_t appInstanceId;

    ::std::unique_ptr<Timer> memoryTimer;
    ::std::unique_ptr<Timer> tasksTimer;

    UsageCardWidgets internalMemCard;
    UsageCardWidgets externalMemCard;
    /** The mounted file systems and their capacity, read on the app's task before the window is created and never changed after that */
    tt::std::vector<StorageVolume> storage;
    /** The mounted file systems with a capacity, which have a card each while the widgets exist */
    tt::std::vector<StorageVolume> volumes;

    /** The list of tasks */
    lv_obj_t* tasksContainer = nullptr;
    /** The task name of each row in tasksContainer */
    tt::std::vector<tt::std::string> taskNames;
#if configUSE_TRACE_FACILITY
    lv_obj_t* taskChipRow = nullptr;
    lv_obj_t* taskChips[TASK_CHIP_COUNT] = {};
    lv_obj_t* taskChipWords[TASK_CHIP_COUNT] = {};
    lv_obj_t* taskChipCounts[TASK_CHIP_COUNT] = {};
    lv_obj_t* taskSubtitle = nullptr;
    lv_obj_t* taskLegend = nullptr;
    /** 0 shows all tasks, and otherwise a TaskGroup + 1 */
    size_t taskFilter = 0;
    /** The task of which the details are shown */
    tt::std::string expandedTask;
#endif
    lv_obj_t* psramContainer = nullptr;
    lv_obj_t* tabs = nullptr;
    lv_obj_t* panes[TAB_COUNT] = {};
    /** Survives a rebuild of the widgets */
    uint32_t selectedTab = 0;

    /** The uptime, which changes while the tab is shown */
    lv_obj_t* uptimeLabel = nullptr;

    bool hasExternalMem = false;
};


void updateMemory(Context* ctx) {
    updateUsageCard(ctx->internalMemCard, getHeapFree(), getHeapTotal(), getHeapLargestBlock());

    if (ctx->hasExternalMem) {
        updateUsageCard(ctx->externalMemCard, getSpiFree(), getSpiTotal(), getSpiLargestBlock());
    }
}

void updateAbout(Context* ctx) {
    if (ctx->uptimeLabel != nullptr) {
        lv_label_set_text(ctx->uptimeLabel, formatUptime(get_micros_since_boot() / 1000000).c_str());
    }
}

void updateStorage(Context* ctx) {
    for (auto& volume : ctx->volumes) {
        updateUsageCard(volume.card, volume.free, volume.total, 0);
    }
}

#if configUSE_TRACE_FACILITY

void updateTasks(Context* ctx);

void onTaskChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_current_target_obj(event);
    for (size_t i = 0; i < TASK_CHIP_COUNT; i++) {
        if (ctx->taskChips[i] == chip) {
            ctx->taskFilter = i;
        }
    }
    updateTasks(ctx);
}

void onTaskPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const auto index = static_cast<size_t>(lv_obj_get_index(lv_event_get_current_target_obj(event)));
    if (index >= ctx->taskNames.size()) {
        return;
    }
    // The details of one task show at a time
    ctx->expandedTask = ctx->expandedTask == ctx->taskNames[index] ? "" : ctx->taskNames[index];
    updateTasks(ctx);
}

/** "[shape] [word] [count]": the word is only shown for the selected chip, or for all when there is room */
void createTaskChips(lv_obj_t* parent, Context* ctx) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(row, false);
    ctx->taskChipRow = row;

    for (size_t i = 0; i < TASK_CHIP_COUNT; i++) {
        auto* chip = lvgl_chip_create(row);
        // Lays out the shape, the word and the count next to each other
        lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(chip, LV_DPX(4), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(chip, onTaskChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
        ctx->taskChips[i] = chip;
        if (i > 0) {
            const auto group = static_cast<TaskGroup>(i - 1);
            auto* icon = lv_label_create(chip);
            lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
            lv_label_set_text(icon, getGroupIcon(group));
        }
        auto* word = lv_label_create(chip);
        lv_label_set_text(word, i == 0 ? tr("All") : getGroupWord(static_cast<TaskGroup>(i - 1)));
        ctx->taskChipWords[i] = word;
        ctx->taskChipCounts[i] = lv_label_create(chip);
    }
    lvgl_grid_navigation_add(row);

    ctx->taskSubtitle = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->taskSubtitle, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(ctx->taskSubtitle, LV_DPX(8), LV_STATE_DEFAULT);

    // Shows the shapes with their words, when the chips don't have room to
    auto* legend = lv_obj_create(parent);
    lv_obj_remove_style_all(legend);
    lv_obj_set_size(legend, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(legend, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(legend, LV_DPX(12), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(legend, false);
    for (size_t i = 0; i < TASK_CHIP_COUNT - 1; i++) {
        const auto group = static_cast<TaskGroup>(i);
        // The shape is in the icon font, which the text font doesn't have
        auto* item = lv_obj_create(legend);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, LV_DPX(4), LV_STATE_DEFAULT);
        lv_obj_set_scrollable(item, false);
        auto* icon = lv_label_create(item);
        lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_label_set_text(icon, getGroupIcon(group));
        auto* word = lv_label_create(item);
        lv_obj_set_style_text_font(word, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_label_set_text(word, getGroupWord(group));
    }
    ctx->taskLegend = legend;
}

/** The first child is the shape, then the name, the stack left and the details, which only show for the selected task */
lv_obj_t* createTaskRow(lv_obj_t* list, Context* ctx) {
    auto* row = lvgl_list_add_button(list, nullptr, nullptr);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, onTaskPressed, LV_EVENT_SHORT_CLICKED, ctx);

    auto* icon = lv_label_create(row);
    lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);

    auto* name = lv_label_create(row);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

    auto* stack = lv_label_create(row);
    lv_obj_set_style_text_font(stack, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    auto* details = lv_obj_create(row);
    // Without the theme's styles, the text colour of the row applies, which follows its focused and pressed states
    lv_obj_remove_style_all(details);
    lv_obj_set_size(details, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(details, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_left(details, LV_DPX(24), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(details, LV_DPX(12), LV_STATE_DEFAULT);
    lv_obj_set_clickable(details, false);
    lv_obj_set_scrollable(details, false);
    lv_obj_set_hidden(details, true);
    auto* labels = lv_label_create(details);
    lv_obj_set_style_text_font(labels, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_text_fmt(labels, "%s\n%s\n%s", tr("State"), tr("Priority"), tr("Stack minimum"));
    auto* values = lv_label_create(details);
    lv_obj_set_flex_grow(values, 1);
    lv_obj_set_style_text_font(values, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(values, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
    return row;
}

void updateTaskRow(lv_obj_t* row, const TaskInfo& task, bool expanded) {
    auto* icon = lv_obj_get_child(row, 0);
    lv_label_set_text(icon, getGroupIcon(task.group));
    setGroupColor(icon, task.group);
    lv_label_set_text(lv_obj_get_child(row, 1), task.name.c_str());

    auto* stack = lv_obj_get_child(row, 2);
    lv_label_set_text(stack, formatSize(task.stackFree).c_str());
    if (!lvgl_theme_is_mono() && task.stackFree < LOW_STACK_BYTES) {
        lv_obj_set_style_text_color(stack, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);
    } else {
        lv_obj_remove_local_style_prop(stack, LV_STYLE_TEXT_COLOR, LV_PART_MAIN);
    }

    auto* details = lv_obj_get_child(row, 3);
    lv_obj_set_hidden(details, !expanded);
    if (expanded) {
        lv_label_set_text_fmt(lv_obj_get_child(details, 1), "%s\n%u\n%s", getGroupWord(task.group), static_cast<unsigned>(task.priority), formatSize(task.stackFree).c_str());
    }
}

/** The words are shown for all chips when they fit in a row. Otherwise the selected one has its word, and the legend explains the shapes. */
void updateTaskChipWords(Context* ctx) {
    for (size_t i = 1; i < TASK_CHIP_COUNT; i++) {
        lv_obj_set_hidden(ctx->taskChipWords[i], false);
    }
    lv_obj_update_layout(ctx->taskChipRow);
    const bool wrapped = lv_obj_get_y(ctx->taskChips[TASK_CHIP_COUNT - 1]) > lv_obj_get_y(ctx->taskChips[0]);
    if (wrapped) {
        for (size_t i = 1; i < TASK_CHIP_COUNT; i++) {
            lv_obj_set_hidden(ctx->taskChipWords[i], i != ctx->taskFilter);
        }
    }
    // The legend needs room for itself as well
    lv_obj_set_hidden(ctx->taskLegend, !wrapped || lv_obj_get_height(lv_obj_get_parent(ctx->taskChipRow)) < LV_DPX(240));
}

/** Updates the rows in place, so the list keeps its scroll position, and the selected task stays selected */
void updateTaskRows(Context* ctx, const tt::std::vector<const TaskInfo*>& shown) {
    auto* list = ctx->tasksContainer;
    lv_obj_t* selected = lvgl_grid_navigation_get_focused(list);
    const int32_t selected_index = selected != nullptr ? lv_obj_get_index(selected) : -1;
    const tt::std::string selected_name = selected_index >= 0 && selected_index < static_cast<int32_t>(ctx->taskNames.size()) ? ctx->taskNames[selected_index] : "";
    const bool show_selection = selected != nullptr && lv_obj_has_state(selected, LV_STATE_FOCUS_KEY);

    ctx->taskNames.clear();
    for (size_t i = 0; i < shown.size(); i++) {
        auto* row = i < lv_obj_get_child_count(list) ? lv_obj_get_child(list, static_cast<int32_t>(i)) : createTaskRow(list, ctx);
        updateTaskRow(row, *shown[i], shown[i]->name == ctx->expandedTask);
        ctx->taskNames.emplace_back(shown[i]->name);
    }
    while (lv_obj_get_child_count(list) > shown.size()) {
        lv_obj_delete(lv_obj_get_child(list, -1));
    }
    // A row that was the last one has no divider, and gets it back when rows follow it
    const uint32_t row_count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < row_count; i++) {
        lv_obj_remove_local_style_prop(lv_obj_get_child(list, static_cast<int32_t>(i)), LV_STYLE_BORDER_WIDTH, LV_PART_MAIN);
    }
    lvgl_list_hide_last_divider(list);

    if (selected_index < 0 || ctx->taskNames.empty()) {
        return;
    }
    // Follows the task, or keeps the position when the task is gone
    auto it = ::std::ranges::find(ctx->taskNames, selected_name);
    const auto index = it != ctx->taskNames.end() ? static_cast<int32_t>(it - ctx->taskNames.begin()) : ::std::min(selected_index, static_cast<int32_t>(ctx->taskNames.size()) - 1);
    // Also re-applied when the row didn't change: deleting rows resets grid navigation's selection
    auto* row = lv_obj_get_child(list, index);
    lv_gridnav_set_focused(list, row, LV_ANIM_OFF);
    const bool list_focused = lv_obj_get_group(list) != nullptr && lv_group_get_focused(lv_obj_get_group(list)) == list;
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

void updateTasks(Context* ctx) {
#if configUSE_TRACE_FACILITY
    if (ctx->tasksContainer == nullptr) {
        return;
    }
    const auto tasks = collectTasks();
    size_t counts[TASK_CHIP_COUNT] = { tasks.size(), 0, 0, 0, 0 };
    for (const auto& task : tasks) {
        counts[static_cast<size_t>(task.group) + 1]++;
    }
    tt::std::vector<const TaskInfo*> shown;
    for (const auto& task : tasks) {
        if (ctx->taskFilter == 0 || static_cast<size_t>(task.group) + 1 == ctx->taskFilter) {
            shown.push_back(&task);
        }
    }

    for (size_t i = 0; i < TASK_CHIP_COUNT; i++) {
        lv_label_set_text_fmt(ctx->taskChipCounts[i], "%u", static_cast<unsigned>(counts[i]));
        lv_obj_set_state(ctx->taskChips[i], LV_STATE_CHECKED, i == ctx->taskFilter);
        if (i > 0) {
            // The selected chip has its own colours, which the icon follows to keep its contrast
            auto* icon = lv_obj_get_child(ctx->taskChips[i], 0);
            if (i == ctx->taskFilter) {
                lv_obj_remove_local_style_prop(icon, LV_STYLE_TEXT_COLOR, LV_PART_MAIN);
            } else {
                setGroupColor(icon, static_cast<TaskGroup>(i - 1));
            }
        }
    }
    lv_label_set_text(ctx->taskSubtitle, getGroupDescription(ctx->taskFilter));
    updateTaskChipWords(ctx);
    updateTaskRows(ctx, shown);
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
    for (const Tab tab : { Tab::Memory, Tab::Storage, Tab::About }) {
        setFocusable(group, ctx->panes[static_cast<size_t>(tab)], ctx->selectedTab == static_cast<uint32_t>(tab));
    }
#if configUSE_TRACE_FACILITY
    setFocusable(group, ctx->taskChipRow, ctx->selectedTab == static_cast<uint32_t>(Tab::Tasks));
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
    for (auto* pane : { memory_tab, storage_tab, about_tab }) {
        lv_obj_set_style_pad_row(pane, lvgl_theme_lerp(LV_DPX(4), LV_DPX(8), lvgl_theme_get_spacing()), LV_STATE_DEFAULT);
        lvgl_grid_navigation_add(pane);
    }

    // Memory tab content
    createUsageCard(memory_tab, tr("Internal RAM"), ctx->internalMemCard);

    ctx->hasExternalMem = getSpiTotal() > 0;
    if (ctx->hasExternalMem) {
        createUsageCard(memory_tab, tr("PSRAM"), ctx->externalMemCard);
    }

    // Storage tab content
    ctx->volumes.clear();
    ctx->uptimeLabel = nullptr;
    ctx->volumes = ctx->storage;
    for (auto& volume : ctx->volumes) {
        createUsageCard(storage_tab, volume.name.c_str(), volume.card);
    }

#if configUSE_TRACE_FACILITY
    // Tasks tab: the filter, then the list that takes the rest of the tab and scrolls by itself
    lv_obj_set_scrollable(tasks_tab, false);
    createTaskChips(tasks_tab, ctx);
    auto* tasks_card = lvgl_card_create(tasks_tab);
    lv_obj_set_size(tasks_card, LV_PCT(100), 0);
    lv_obj_set_flex_grow(tasks_card, 1);
    lv_obj_set_style_pad_all(tasks_card, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(tasks_card, false);
    // The rows' pressed and focused backgrounds follow the card's rounded corners
    lv_obj_set_style_clip_corner(tasks_card, true, LV_STATE_DEFAULT);
    // The card provides the background
    ctx->tasksContainer = lvgl_list_create(tasks_card);
    lv_obj_set_size(ctx->tasksContainer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(ctx->tasksContainer, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ctx->tasksContainer, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->tasksContainer, 0, LV_STATE_DEFAULT);
    lvgl_grid_navigation_add(ctx->tasksContainer);
#endif
    showSelectedTab(ctx);

    // About tab content: the name and version in the middle, then what the device and the software are
    auto* about_header = lv_obj_create(about_tab);
    lv_obj_remove_style_all(about_header);
    lv_obj_set_size(about_header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(about_header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(about_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(about_header, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_clickable(about_header, false);
    lv_obj_set_scrollable(about_header, false);
    auto* about_name = lv_label_create(about_header);
    lv_label_set_text(about_name, "Tactility");
    lv_obj_set_style_text_font(about_name, lvgl_get_text_font(FONT_SIZE_LARGE), LV_STATE_DEFAULT);
    auto* about_version = lv_label_create(about_header);
    lv_label_set_text(about_version, i18n::format(tr("Version {}"), { TT_VERSION }).c_str());
    lv_obj_set_style_text_opa(about_version, LV_OPA_70, LV_STATE_DEFAULT);

    auto* device_card = createInfoCard(about_tab, tr("Device"));
    addInfoRow(device_card, tr("Vendor"), CONFIG_TT_DEVICE_VENDOR);
    addInfoRow(device_card, tr("Model"), CONFIG_TT_DEVICE_NAME_SIMPLE);
#ifdef ESP_PLATFORM
    addInfoRow(device_card, tr("Chip"), i18n::format(tr("{} · {} cores"), { getChipName().c_str(), ::std::to_string(SOC_CPU_CORES_NUM) }).c_str());
    addInfoRow(device_card, tr("Flash"), CONFIG_ESPTOOLPY_FLASHSIZE);
#endif
    if (ctx->hasExternalMem) {
        addInfoRow(device_card, tr("PSRAM"), formatSize(getSpiTotal()).c_str());
    }
    Device* display = nullptr;
    if (device_get_first_by_type(&DISPLAY_TYPE, &display) == ERROR_NONE) {
        tt::std::string resolution = ::std::to_string(display_get_resolution_x(display)).c_str();
        resolution += " × ";
        resolution += ::std::to_string(display_get_resolution_y(display)).c_str();
        addInfoRow(device_card, tr("Display"), resolution.c_str());
        device_put(display);
    }

    auto* software_card = createInfoCard(about_tab, tr("Software"));
#ifdef ESP_PLATFORM
    tt::std::string idfVersion = ::std::to_string(ESP_IDF_VERSION_MAJOR).c_str();
    idfVersion += ".";
    idfVersion += ::std::to_string(ESP_IDF_VERSION_MINOR).c_str();
    idfVersion += ".";
    idfVersion += ::std::to_string(ESP_IDF_VERSION_PATCH).c_str();
    addInfoRow(software_card, "ESP-IDF", idfVersion.c_str());
#endif
    ctx->uptimeLabel = addInfoRow(software_card, tr("Uptime"), "");

    // Initial updates
    updateMemory(ctx);
    updateStorage(ctx);  // Storage: from the snapshot of the app start
    updateAbout(ctx);
    updateTasks(ctx);
}

/** The widgets are deleted when the window is buried. The pointers to them are cleared, and the timers keep running without updating them. */
void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->internalMemCard = UsageCardWidgets {};
    ctx->externalMemCard = UsageCardWidgets {};
    ctx->volumes.clear();
    ctx->tasksContainer = nullptr;
    ctx->taskNames.clear();
#if configUSE_TRACE_FACILITY
    ctx->taskChipRow = nullptr;
    for (size_t i = 0; i < TASK_CHIP_COUNT; i++) {
        ctx->taskChips[i] = nullptr;
        ctx->taskChipWords[i] = nullptr;
        ctx->taskChipCounts[i] = nullptr;
    }
    ctx->taskSubtitle = nullptr;
    ctx->taskLegend = nullptr;
#endif
    ctx->uptimeLabel = nullptr;
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
    // The timers can't be stopped when the widgets are destroyed, as stopping waits for a callback that waits for the LVGL lock.
    // While the widgets don't exist, the callbacks only check for that.
    ctx.memoryTimer = ::std::make_unique<Timer>(Timer::Type::Periodic, millis_to_ticks(10000), [&ctx] {
        lvgl_lock();
        if (ctx.tabs != nullptr) {
            updateMemory(&ctx);
            updateAbout(&ctx);
        }
        lvgl_unlock();
    });

    ctx.tasksTimer = ::std::make_unique<Timer>(Timer::Type::Periodic, millis_to_ticks(15000), [&ctx] {
        lvgl_lock();
        if (ctx.tabs != nullptr) {
            updateTasks(&ctx);
        }
        lvgl_unlock();
    });

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    // Before the window exists, so the LVGL task never waits for the file systems
    file_system_for_each_mounted(&ctx.storage, collectStorageVolume);

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
