#include "lvgl/icons/shared.h"
#include "lvgl/theme.h"


#include "tactility/time.h"
#include <tactility/check.h>

#include <Tactility/DeprecatedPaths.h>
#include <Tactility/TactilityConfig.h>
#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <algorithm>
#include <strings.h>
#include <format>
#include <utility>
#include <vector>
#include <string>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#ifdef ESP_PLATFORM
#include <esp_vfs_fat.h>
#include <esp_heap_caps.h>
#include <Tactility/MountPoints.h>
#endif

namespace tt::app::systeminfo {

constexpr auto* TAG = "SystemInfo";

extern const ::AppManifest manifest;

namespace {

size_t getHeapFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#else
    return 4096 * 1024;
#endif
}

size_t getHeapTotal() {
#ifdef ESP_PLATFORM
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
#else
    return 8192 * 1024;
#endif
}

size_t getSpiFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
    return 4096 * 1024;
#endif
}

size_t getSpiTotal() {
#ifdef ESP_PLATFORM
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
#else
    return 8192 * 1024;
#endif
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

struct MemoryBarWidgets {
    lv_obj_t* bar = nullptr;
    lv_obj_t* label = nullptr;
};

MemoryBarWidgets createMemoryBar(lv_obj_t* parent, const char* label) {
    auto* container = lv_obj_create(parent);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(container, 0, LV_STATE_DEFAULT);

    auto* left_label = lv_label_create(container);
    lv_label_set_text(left_label, label);
    auto label_width = 6 * lvgl_get_text_font_height(FONT_SIZE_DEFAULT);
    lv_obj_set_width(left_label, label_width);

    auto* bar = lv_bar_create(container);
    lv_obj_set_flex_grow(bar, 1);

    auto* bottom_label = lv_label_create(parent);
    lv_obj_set_width(bottom_label, LV_PCT(100));
    lv_obj_set_style_text_align(bottom_label, LV_TEXT_ALIGN_RIGHT, 0);

    return {bar, bottom_label};
}

void updateMemoryBar(const MemoryBarWidgets& widgets, uint64_t free, uint64_t total) {
    uint64_t used = total - free;

    // Scale down the uint64_t until it fits int32_t for the lv_bar
    uint64_t free_scaled = free;
    uint64_t total_scaled = total;
    while (total_scaled > static_cast<uint64_t>(INT32_MAX)) {
        free_scaled /= 1024;
        total_scaled /= 1024;
    }

    if (total > 0) {
        lv_bar_set_range(widgets.bar, 0, total_scaled);
    } else {
        lv_bar_set_range(widgets.bar, 0, 1);
    }

    lv_bar_set_value(widgets.bar, (total_scaled - free_scaled), LV_ANIM_OFF);

    const auto unit = getStorageUnit(total);
    const auto unit_label = getStorageUnitString(unit);
    const auto free_converted = getStorageValue(unit, free);
    const auto total_converted = getStorageValue(unit, total);
    lv_label_set_text_fmt(widgets.label, "%s / %s %s free (%llu / %llu bytes)",
        free_converted.c_str(), total_converted.c_str(), unit_label.c_str(),
        (unsigned long long)free, (unsigned long long)total);
}

#if configUSE_TRACE_FACILITY

const char* getTaskState(const TaskStatus_t& task) {
    switch (task.eCurrentState) {
        case eRunning:
            return "running";
        case eReady:
            return "ready";
        case eBlocked:
            return "blocked";
        case eSuspended:
            return "suspended";
        case eDeleted:
            return "deleted";
        case eInvalid:
        default:
            return "invalid";
    }
}

const char* getTaskName(const TaskStatus_t& task) {
    return (task.pcTaskName == nullptr || task.pcTaskName[0] == 0) ? "(unnamed)" : task.pcTaskName;
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
        lv_label_set_text(error_label, "Failed to allocate memory for task list");
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
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
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

lv_obj_t* createTab(lv_obj_t* tabview, const char* name) {
    auto* tab = lv_tabview_add_tab(tabview, name);
    auto* tab_bar = lv_tabview_get_tab_bar(tabview);
    auto* tab_button = lv_obj_get_child(tab_bar, static_cast<int32_t>(lv_obj_get_child_count(tab_bar)) - 1);
    auto* shared_icon_font = lvgl_theme_is_compact() ? lvgl_get_shared_icon_default_font() : lvgl_get_shared_icon_large_font();
    lv_obj_set_style_text_font(tab_button, shared_icon_font, LV_STATE_DEFAULT);
    // Square buttons, so the theme's round selection indicator fits the icon
    const int32_t button_size = lv_font_get_line_height(shared_icon_font) + 2 * lv_obj_get_style_pad_top(tab_button, LV_PART_MAIN);
    lv_obj_set_flex_grow(tab_button, 0);
    lv_obj_set_size(tab_button, button_size, button_size);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(tab, 0, LV_STATE_DEFAULT);
    return tab;
}

struct Context {
    uint32_t appInstanceId;

    std::unique_ptr<Timer> memoryTimer;
    std::unique_ptr<Timer> tasksTimer;

    MemoryBarWidgets internalMemBar;
    MemoryBarWidgets externalMemBar;
    MemoryBarWidgets dataStorageBar;
    MemoryBarWidgets sdcardStorageBar;
    MemoryBarWidgets systemStorageBar;

    lv_obj_t* tasksContainer = nullptr;
    /** The task name of each row in tasksContainer */
    std::vector<std::string> taskNames;
    lv_obj_t* psramContainer = nullptr;

    bool hasExternalMem = false;
    bool hasDataStorage = false;
    bool hasSystemStorage = false;
};


void updateMemory(Context* ctx) {
    updateMemoryBar(ctx->internalMemBar, getHeapFree(), getHeapTotal());

    if (ctx->hasExternalMem) {
        updateMemoryBar(ctx->externalMemBar, getSpiFree(), getSpiTotal());
    }
}

void updateStorage(Context* ctx) {
#ifdef ESP_PLATFORM
    uint64_t storage_total = 0;
    uint64_t storage_free = 0;

    if (ctx->hasDataStorage) {
        if (esp_vfs_fat_info(file::MOUNT_POINT_DATA, &storage_total, &storage_free) == ESP_OK) {
            updateMemoryBar(ctx->dataStorageBar, storage_free, storage_total);
        }
    }

    std::string sdcard_path;
    if (findFirstMountedSdCardPath(sdcard_path) && esp_vfs_fat_info(sdcard_path.c_str(), &storage_total, &storage_free) == ESP_OK) {
        updateMemoryBar(ctx->sdcardStorageBar, storage_free, storage_total);
    }

    if (ctx->hasSystemStorage) {
        if (esp_vfs_fat_info(file::MOUNT_POINT_SYSTEM, &storage_total, &storage_free) == ESP_OK) {
            updateMemoryBar(ctx->systemStorageBar, storage_free, storage_total);
        }
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

#if configUSE_TRACE_FACILITY
void onTabChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* tabview = lv_event_get_target_obj(event);
    auto* active_tab = lv_obj_get_child(lv_tabview_get_content(tabview), static_cast<int32_t>(lv_tabview_get_tab_active(tabview)));
    const bool tasks_shown = active_tab == lv_obj_get_parent(ctx->tasksContainer);
    auto* group = lv_group_get_default();
    if (group == nullptr) {
        return;
    }
    if (tasks_shown && lv_obj_get_group(ctx->tasksContainer) == nullptr) {
        lv_group_add_obj(group, ctx->tasksContainer);
    } else if (!tasks_shown && lv_obj_get_group(ctx->tasksContainer) != nullptr) {
        lv_group_remove_obj(ctx->tasksContainer);
    }
}
#endif

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "System Info");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_pad_all(wrapper, 0, LV_STATE_DEFAULT);
    // Space between the screen edges and the rounded tab bar and content
    const int32_t edge_space = lvgl_theme_is_compact() ? 2 : LV_DPX(8);
    lv_obj_set_style_pad_hor(wrapper, edge_space, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(wrapper, edge_space, LV_STATE_DEFAULT);

    auto* tabview = lv_tabview_create(wrapper);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);

    // Create tabs
    auto* memory_tab = createTab(tabview, LVGL_ICON_SHARED_MEMORY);
    auto* storage_tab = createTab(tabview, LVGL_ICON_SHARED_HARD_DISK);
    auto* tasks_tab = createTab(tabview, LVGL_ICON_SHARED_SELECT_WINDOW_2);
    auto* about_tab = createTab(tabview, LVGL_ICON_SHARED_INFO);

    // As wide as the buttons, which are spread over the bar's height
    auto* tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_width(tab_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(tab_bar, lvgl_theme_is_compact() ? 2 : LV_DPX(12), LV_STATE_DEFAULT);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Memory tab content
    ctx->internalMemBar = createMemoryBar(memory_tab, "Internal");

    ctx->hasExternalMem = getSpiTotal() > 0;
    if (ctx->hasExternalMem) {
        ctx->externalMemBar = createMemoryBar(memory_tab, "External");
    }

#ifdef ESP_PLATFORM
    // Storage tab content
    uint64_t storage_total = 0;
    uint64_t storage_free = 0;

    ctx->hasDataStorage = (esp_vfs_fat_info(file::MOUNT_POINT_DATA, &storage_total, &storage_free) == ESP_OK);
    if (ctx->hasDataStorage) {
        ctx->dataStorageBar = createMemoryBar(storage_tab, file::MOUNT_POINT_DATA);
    }

    std::string sdcard_path;
    if (findFirstMountedSdCardPath(sdcard_path) && esp_vfs_fat_info(sdcard_path.c_str(), &storage_total, &storage_free) == ESP_OK) {
        ctx->sdcardStorageBar = createMemoryBar(storage_tab, sdcard_path.c_str());
    }

    if (config::SHOW_SYSTEM_PARTITION) {
        ctx->hasSystemStorage = (esp_vfs_fat_info(file::MOUNT_POINT_SYSTEM, &storage_total, &storage_free) == ESP_OK);
        if (ctx->hasSystemStorage) {
            ctx->systemStorageBar = createMemoryBar(storage_tab, file::MOUNT_POINT_SYSTEM);
        }
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
    // Only the shown tab's list can be focused, as focusing another tab's content would scroll to it
    lv_group_remove_obj(ctx->tasksContainer);
    lv_obj_add_event_cb(tabview, onTabChanged, LV_EVENT_VALUE_CHANGED, ctx);
#endif

    // Build info
    auto* tactility_version = lv_label_create(about_tab);
    lv_label_set_text_fmt(tactility_version, "Tactility v%s", TT_VERSION);
#ifdef ESP_PLATFORM
    auto* esp_idf_version = lv_label_create(about_tab);
    lv_label_set_text_fmt(esp_idf_version, "ESP-IDF v%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
#endif

    auto* device_vendor = lv_label_create(about_tab);
    lv_label_set_text_fmt(device_vendor, "Hardware vendor: %s", CONFIG_TT_DEVICE_VENDOR);
    auto* device_device_name = lv_label_create(about_tab);
    lv_label_set_text_fmt(device_device_name, "Hardware model: %s", CONFIG_TT_DEVICE_NAME_SIMPLE);

    // Initial updates
    updateMemory(ctx);
    updateStorage(ctx);  // Storage: one-time update on show (doesn't change frequently)
    updateTasks(ctx);
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

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);
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
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace
