#include <Tactility/TactilityConfig.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>

#include <Tactility/Paths.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <lvgl.h>
#include <utility>

#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_icon_shared.h>
#include <tactility/lvgl_module.h>

#ifdef ESP_PLATFORM
#include <esp_vfs_fat.h>
#include <esp_heap_caps.h>
#include <Tactility/MountPoints.h>
#endif

namespace tt::app::systeminfo {

constexpr auto* TAG = "SystemInfo";

static size_t getHeapFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#else
    return 4096 * 1024;
#endif
}

static size_t getHeapTotal() {
#ifdef ESP_PLATFORM
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
#else
    return 8192 * 1024;
#endif
}

static size_t getSpiFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
    return 4096 * 1024;
#endif
}

static size_t getSpiTotal() {
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

static StorageUnit getStorageUnit(uint64_t value) {
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

static std::string getStorageUnitString(StorageUnit unit) {
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

static std::string getStorageValue(StorageUnit unit, uint64_t bytes) {
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

static MemoryBarWidgets createMemoryBar(lv_obj_t* parent, const char* label) {
    auto* container = lv_obj_create(parent);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
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

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_bottom(bottom_label, 2, LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_pad_bottom(bottom_label, 12, LV_STATE_DEFAULT);
    }

    return {bar, bottom_label};
}

static void updateMemoryBar(const MemoryBarWidgets& widgets, uint64_t free, uint64_t total) {
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

static const char* getTaskState(const TaskStatus_t& task) {
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

static void clearContainer(lv_obj_t* container) {
    lv_obj_clean(container);
}

static void addRtosTask(lv_obj_t* parent, const TaskStatus_t& task) {
    auto* label = lv_label_create(parent);
    const char* name = (task.pcTaskName == nullptr || task.pcTaskName[0] == 0) ? "(unnamed)" : task.pcTaskName;
    lv_label_set_text_fmt(label, "%s (%s)", name, getTaskState(task));
}

static void updateRtosTasks(lv_obj_t* parent) {
    clearContainer(parent);

    UBaseType_t count = uxTaskGetNumberOfTasks();
    auto* tasks = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * count);
    if (!tasks) {
        auto* error_label = lv_label_create(parent);
        lv_label_set_text(error_label, "Failed to allocate memory for task list");
        return;
    }
    uint32_t totalRuntime = 0;
    UBaseType_t actual = uxTaskGetSystemState(tasks, count, &totalRuntime);

    for (int i = 0; i < actual; ++i) {
        addRtosTask(parent, tasks[i]);
    }

    free(tasks);
}

#endif

static lv_obj_t* createTab(lv_obj_t* tabview, const char* name) {
    auto* tab = lv_tabview_add_tab(tabview, name);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(tab, 0, LV_STATE_DEFAULT);
    return tab;
}

extern const AppManifest manifest;

class SystemInfoApp;

static std::shared_ptr<SystemInfoApp> optApp() {
    auto appContext = getCurrentAppContext();
    if (appContext != nullptr && appContext->getManifest().appId == manifest.appId) {
        return std::static_pointer_cast<SystemInfoApp>(appContext->getApp());
    }
    return nullptr;
}

class SystemInfoApp final : public App {
    Timer memoryTimer = Timer(Timer::Type::Periodic, kernel::millisToTicks(10000), [] {
        auto app = optApp();
        if (app) {
            auto lock = lvgl::getSyncLock()->asScopedLock();
            lock.lock();
            app->updateMemory();
        }
    });

    Timer tasksTimer = Timer(Timer::Type::Periodic, kernel::millisToTicks(15000), [] {
        auto app = optApp();
        if (app) {
            auto lock = lvgl::getSyncLock()->asScopedLock();
            lock.lock();
            app->updateTasks();
        }
    });

    MemoryBarWidgets internalMemBar;
    MemoryBarWidgets externalMemBar;
    MemoryBarWidgets dataStorageBar;
    MemoryBarWidgets sdcardStorageBar;
    MemoryBarWidgets systemStorageBar;

    lv_obj_t* tasksContainer = nullptr;
    lv_obj_t* psramContainer = nullptr;

    bool hasExternalMem = false;
    bool hasDataStorage = false;
    bool hasSystemStorage = false;

    void updateMemory() {
        updateMemoryBar(internalMemBar, getHeapFree(), getHeapTotal());

        if (hasExternalMem) {
            updateMemoryBar(externalMemBar, getSpiFree(), getSpiTotal());
        }
    }

    void updateStorage() {
#ifdef ESP_PLATFORM
        uint64_t storage_total = 0;
        uint64_t storage_free = 0;

        if (hasDataStorage) {
            if (esp_vfs_fat_info(file::MOUNT_POINT_DATA, &storage_total, &storage_free) == ESP_OK) {
                updateMemoryBar(dataStorageBar, storage_free, storage_total);
            }
        }

        std::string sdcard_path;
        if (findFirstMountedSdCardPath(sdcard_path) && esp_vfs_fat_info(sdcard_path.c_str(), &storage_total, &storage_free) == ESP_OK) {
            updateMemoryBar(sdcardStorageBar, storage_free, storage_total);
        }

        if (hasSystemStorage) {
            if (esp_vfs_fat_info(file::MOUNT_POINT_SYSTEM, &storage_total, &storage_free) == ESP_OK) {
                updateMemoryBar(systemStorageBar, storage_free, storage_total);
            }
        }
#endif
    }

    void updateTasks() {
#if configUSE_TRACE_FACILITY
        if (tasksContainer) {
            updateRtosTasks(tasksContainer);  // Tasks tab: show state
        }
#endif
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);
        lvgl::toolbar_create(parent, app);

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_style_pad_all(wrapper, 0, LV_STATE_DEFAULT);

        auto* tabview = lv_tabview_create(wrapper);
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);
        auto tab_bar_width = 6 * lvgl_get_text_font_height(FONT_SIZE_DEFAULT);
        lv_tabview_set_tab_bar_size(tabview, tab_bar_width);

        // Create tabs
        auto* memory_tab = createTab(tabview, "Memory");
        auto* storage_tab = createTab(tabview, "Storage");
        auto* tasks_tab = createTab(tabview, "Tasks");
        auto* about_tab = createTab(tabview, "About");

        // Memory tab content
        internalMemBar = createMemoryBar(memory_tab, "Internal");

        hasExternalMem = getSpiTotal() > 0;
        if (hasExternalMem) {
            externalMemBar = createMemoryBar(memory_tab, "External");
        }

#ifdef ESP_PLATFORM
        // Storage tab content
        uint64_t storage_total = 0;
        uint64_t storage_free = 0;

        hasDataStorage = (esp_vfs_fat_info(file::MOUNT_POINT_DATA, &storage_total, &storage_free) == ESP_OK);
        if (hasDataStorage) {
            dataStorageBar = createMemoryBar(storage_tab, file::MOUNT_POINT_DATA);
        }

        std::string sdcard_path;
        if (findFirstMountedSdCardPath(sdcard_path) && esp_vfs_fat_info(sdcard_path.c_str(), &storage_total, &storage_free) == ESP_OK) {
            sdcardStorageBar = createMemoryBar(storage_tab, sdcard_path.c_str());
        }

        if (config::SHOW_SYSTEM_PARTITION) {
            hasSystemStorage = (esp_vfs_fat_info(file::MOUNT_POINT_SYSTEM, &storage_total, &storage_free) == ESP_OK);
            if (hasSystemStorage) {
                systemStorageBar = createMemoryBar(storage_tab, file::MOUNT_POINT_SYSTEM);
            }
        }
#endif

#if configUSE_TRACE_FACILITY
        // Tasks tab - container for dynamic updates
        tasksContainer = lv_obj_create(tasks_tab);
        lv_obj_set_size(tasksContainer, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(tasksContainer, 8, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(tasksContainer, 0, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(tasksContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(tasksContainer, 0, LV_STATE_DEFAULT);
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
        updateMemory();
        updateStorage();  // Storage: one-time update on show (doesn't change frequently)
        updateTasks();

        // Start timers (only run while app is visible, stopped in onHide)
        memoryTimer.start();   // Memory: every 10s
        tasksTimer.start();    // Tasks/CPU: every 15s
    }

    void onHide(AppContext& app) override {
        memoryTimer.stop();
        tasksTimer.stop();
    }
};

extern const AppManifest manifest = {
    .appId = "SystemInfo",
    .appName = "System Info",
    .appIcon = LVGL_ICON_SHARED_AREA_CHART,
    .appCategory = Category::System,
    .createApp = create<SystemInfoApp>
};

} // namespace
