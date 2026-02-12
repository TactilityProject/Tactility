#include <Tactility/TactilityConfig.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>

#include <Tactility/Assets.h>
#include <tactility/hal/Device.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>

#include <algorithm>
#include <format>
#include <lvgl.h>
#include <tactility/lvgl_symbols_shared.h>
#include <utility>
#include <cstring>

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

static size_t getPsramMinFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
#else
    return 4096 * 1024;
#endif
}

static size_t getPsramLargestBlock() {
#ifdef ESP_PLATFORM
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#else
    return 4096 * 1024;
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
    lv_obj_set_width(left_label, 60);

    auto* bar = lv_bar_create(container);
    lv_obj_set_flex_grow(bar, 1);

    auto* bottom_label = lv_label_create(parent);
    lv_obj_set_width(bottom_label, LV_PCT(100));
    lv_obj_set_style_text_align(bottom_label, LV_TEXT_ALIGN_RIGHT, 0);

    if (hal::getConfiguration()->uiScale == hal::UiScale::Smallest) {
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

static void addRtosTask(lv_obj_t* parent, const TaskStatus_t& task, uint32_t totalRuntime) {
    auto* label = lv_label_create(parent);
    const char* name = (task.pcTaskName == nullptr || task.pcTaskName[0] == 0) ? "(unnamed)" : task.pcTaskName;

    // If totalRuntime provided, show CPU percentage; otherwise just show state
    if (totalRuntime > 0) {
        float cpu_percent = (task.ulRunTimeCounter * 100.0f) / totalRuntime;
        lv_label_set_text_fmt(label, "%s: %.1f%%", name, cpu_percent);
    } else {
        lv_label_set_text_fmt(label, "%s (%s)", name, getTaskState(task));
    }
}

static void updateRtosTasks(lv_obj_t* parent, bool showCpuPercent) {
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

    // Sort by CPU usage if showing percentages, otherwise keep original order
    if (showCpuPercent) {
        std::sort(tasks, tasks + actual, [](const TaskStatus_t& a, const TaskStatus_t& b) {
            return a.ulRunTimeCounter > b.ulRunTimeCounter;
        });
    }

    for (int i = 0; i < actual; ++i) {
        addRtosTask(parent, tasks[i], showCpuPercent ? totalRuntime : 0);
    }

    free(tasks);
}

#endif

static void addDevice(lv_obj_t* parent, const std::shared_ptr<hal::Device>& device) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, device->getName().c_str());
}

static void addDevices(lv_obj_t* parent) {
    auto devices = hal::getDevices();
    for (const auto& device: devices) {
        addDevice(parent, device);
    }
}

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
            app->updatePsram();
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
    lv_obj_t* cpuContainer = nullptr;
    lv_obj_t* psramContainer = nullptr;
    lv_obj_t* cpuSummaryLabel = nullptr;  // Shows overall CPU utilization
    lv_obj_t* taskCountLabel = nullptr;   // Shows active task count
    lv_obj_t* uptimeLabel = nullptr;      // Shows system uptime

    bool hasExternalMem = false;
    bool hasDataStorage = false;
    bool hasSdcardStorage = false;
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

        if (hasSdcardStorage) {
            const auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
            for (const auto& sdcard : sdcard_devices) {
                if (sdcard->isMounted() && esp_vfs_fat_info(sdcard->getMountPath().c_str(), &storage_total, &storage_free) == ESP_OK) {
                    updateMemoryBar(sdcardStorageBar, storage_free, storage_total);
                    break;  // Only update first SD card
                }
            }
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
            updateRtosTasks(tasksContainer, false);  // Tasks tab: show state
        }

        if (cpuContainer) {
            updateRtosTasks(cpuContainer, true);  // CPU tab: show percentages
            
            // Update CPU summary at top of tab
            // Note: FreeRTOS runtime stats accumulate since boot, so percentages 
            // are averages over entire uptime, not instantaneous usage
            if (cpuSummaryLabel && taskCountLabel && uptimeLabel) {
                UBaseType_t count = uxTaskGetNumberOfTasks();
                auto* tasks = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * count);
                if (tasks) {
                    uint32_t totalRuntime = 0;
                    UBaseType_t actual = uxTaskGetSystemState(tasks, count, &totalRuntime);
                    
                    if (totalRuntime > 0 && actual > 0) {
                        // Calculate total CPU usage (100% - idle = usage)
                        uint32_t idleTime = 0;
                        for (int i = 0; i < actual; ++i) {
                            const char* name = tasks[i].pcTaskName;
                            if (name && (strcmp(name, "IDLE0") == 0 || strcmp(name, "IDLE1") == 0)) {
                                idleTime += tasks[i].ulRunTimeCounter;
                            }
                        }
                        
                        float cpuUsage = ((totalRuntime - idleTime) * 100.0f) / totalRuntime;
                        auto summary_text = std::format("Overall CPU Usage: {:.1f}% (avg since boot)", cpuUsage);
                        lv_label_set_text(cpuSummaryLabel, summary_text.c_str());
                        
                        // Show total task count
                        auto core_text = std::format("Active Tasks: {} total", actual);
                        lv_label_set_text(taskCountLabel, core_text.c_str());
                        
                        // Use actual system tick count for uptime
                        TickType_t ticks = xTaskGetTickCount();
                        float uptime_sec = static_cast<float>(ticks) / configTICK_RATE_HZ;
                        auto uptime_text = std::format("System Uptime: {:.1f} min", uptime_sec / 60.0f);
                        lv_label_set_text(uptimeLabel, uptime_text.c_str());
                    } else {
                        lv_label_set_text(cpuSummaryLabel, "Overall CPU Usage: --.-%");
                        lv_label_set_text(taskCountLabel, "Active Tasks: --");
                        lv_label_set_text(uptimeLabel, "System Uptime: --");
                    }
                    
                    free(tasks);
                }
            }
        }
#endif
    }

    void updatePsram() {
#ifdef ESP_PLATFORM
        if (!psramContainer || !hasExternalMem) return;

        clearContainer(psramContainer);

        size_t free_mem = getSpiFree();
        size_t total = getSpiTotal();
        size_t used = total - free_mem;
        size_t min_free = getPsramMinFree();
        size_t largest_block = getPsramLargestBlock();
        size_t peak_usage = total - min_free;

        // Safety check - if no PSRAM, show error
        if (total == 0) {
            auto* error_label = lv_label_create(psramContainer);
            lv_label_set_text(error_label, "No PSRAM detected");
            return;
        }

        // Summary
        auto* summary_label = lv_label_create(psramContainer);
        lv_label_set_text(summary_label, "PSRAM Usage Summary");
        lv_obj_set_style_text_font(summary_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_bottom(summary_label, 8, 0);

        // Current usage
        auto* usage_label = lv_label_create(psramContainer);
        float used_mb = used / (1024.0f * 1024.0f);
        float total_mb = total / (1024.0f * 1024.0f);
        float used_percent = (used * 100.0f) / total;
        auto usage_text = std::format("Current: {:.2f} / {:.2f} MB ({:.1f}% used)",
            used_mb, total_mb, used_percent);
        lv_label_set_text(usage_label, usage_text.c_str());

        // Peak usage
        auto* peak_label = lv_label_create(psramContainer);
        float peak_mb = peak_usage / (1024.0f * 1024.0f);
        float peak_percent = (peak_usage * 100.0f) / total;
        auto peak_text = std::format("Peak: {:.2f} MB ({:.1f}% of total)",
            peak_mb, peak_percent);
        lv_label_set_text(peak_label, peak_text.c_str());

        // Minimum free (lowest point)
        auto* min_free_label = lv_label_create(psramContainer);
        float min_free_mb = min_free / (1024.0f * 1024.0f);
        auto min_free_text = std::format("Min Free: {:.2f} MB", min_free_mb);
        lv_label_set_text(min_free_label, min_free_text.c_str());

        // Largest contiguous block
        auto* largest_label = lv_label_create(psramContainer);
        float largest_mb = largest_block / (1024.0f * 1024.0f);
        auto largest_text = std::format("Largest Block: {:.2f} MB", largest_mb);
        lv_label_set_text(largest_label, largest_text.c_str());

        // Spacer
        auto* spacer = lv_obj_create(psramContainer);
        lv_obj_set_size(spacer, LV_PCT(100), 16);
        lv_obj_set_style_bg_opa(spacer, 0, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);

        // PSRAM Configuration section
        auto* config_header = lv_label_create(psramContainer);
        lv_label_set_text(config_header, "PSRAM Configuration");
        lv_obj_set_style_text_font(config_header, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_bottom(config_header, 8, 0);

        // Get threshold from sdkconfig
#ifdef CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
        const int threshold = CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL;
#else
        const int threshold = 16384; // Default ESP-IDF value
#endif

        // Display threshold configuration
        auto* threshold_info = lv_label_create(psramContainer);
        if (threshold >= 1024) {
            lv_label_set_text_fmt(threshold_info, "• Threshold: >=%d KB -> PSRAM", threshold / 1024);
        } else {
            lv_label_set_text_fmt(threshold_info, "• Threshold: >=%d bytes -> PSRAM", threshold);
        }

        auto* internal_info = lv_label_create(psramContainer);
        if (threshold >= 1024) {
            lv_label_set_text_fmt(internal_info, "• Allocations <%d KB -> Internal RAM", threshold / 1024);
        } else {
            lv_label_set_text_fmt(internal_info, "• Allocations <%d bytes -> Internal RAM", threshold);
        }

        auto* note_label = lv_label_create(psramContainer);
        lv_label_set_text(note_label, "• DMA buffers always use Internal RAM");

        // Spacer after config
        auto* spacer_config = lv_obj_create(psramContainer);
        lv_obj_set_size(spacer_config, LV_PCT(100), 16);
        lv_obj_set_style_bg_opa(spacer_config, 0, 0);
        lv_obj_set_style_border_width(spacer_config, 0, 0);

        // Known PSRAM consumers header
        auto* consumers_label = lv_label_create(psramContainer);
        lv_label_set_text(consumers_label, "PSRAM Allocation Strategy");
        lv_obj_set_style_text_font(consumers_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_bottom(consumers_label, 8, 0);

        // Explain what's in PSRAM
        auto* strategy_note = lv_label_create(psramContainer);
        lv_label_set_text(strategy_note, "Apps don't pre-allocate to PSRAM.\nThey use LVGL dynamic allocation:");
        lv_obj_set_style_text_color(strategy_note, lv_palette_main(LV_PALETTE_GREY), 0);

        // List what automatically goes to PSRAM
        auto* lvgl_label = lv_label_create(psramContainer);
        lv_label_set_text(lvgl_label, "• All LVGL widgets (buttons, labels, etc.)");

        auto* framebuffer_label = lv_label_create(psramContainer);
        lv_label_set_text(framebuffer_label, "• Display framebuffers");

        auto* wifi_label = lv_label_create(psramContainer);
        lv_label_set_text(wifi_label, "• WiFi/Network buffers");

        auto* file_label = lv_label_create(psramContainer);
        lv_label_set_text(file_label, "• File I/O buffers");

        auto* task_label = lv_label_create(psramContainer);
        lv_label_set_text(task_label, "• Task stacks (when enabled)");

        auto* general_label = lv_label_create(psramContainer);
        if (threshold >= 1024) {
            lv_label_set_text_fmt(general_label, "• All allocations >=%d KB", threshold / 1024);
        } else {
            lv_label_set_text_fmt(general_label, "• All allocations >=%d bytes", threshold);
        }

        // Spacer
        auto* spacer_apps = lv_obj_create(psramContainer);
        lv_obj_set_size(spacer_apps, LV_PCT(100), 16);
        lv_obj_set_style_bg_opa(spacer_apps, 0, 0);
        lv_obj_set_style_border_width(spacer_apps, 0, 0);

        // App behavior explanation
        auto* app_behavior_label = lv_label_create(psramContainer);
        lv_label_set_text(app_behavior_label, "App Memory Behavior");
        lv_obj_set_style_text_font(app_behavior_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_bottom(app_behavior_label, 8, 0);

        auto* app_note1 = lv_label_create(psramContainer);
        lv_label_set_text(app_note1, "• Apps allocate UI when opened (10-50 KB)");

        auto* app_note2 = lv_label_create(psramContainer);
        lv_label_set_text(app_note2, "• All app UI goes to PSRAM automatically");

        auto* app_note3 = lv_label_create(psramContainer);
        lv_label_set_text(app_note3, "• Apps deallocate when closed (no caching)");

        auto* app_note4 = lv_label_create(psramContainer);
        lv_label_set_text(app_note4, "• One app open at a time = 10-50 KB in PSRAM");
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
        lv_tabview_set_tab_bar_size(tabview, 80);

        // Create tabs
        auto* memory_tab = createTab(tabview, "Memory");
        auto* psram_tab = createTab(tabview, "PSRAM");
        auto* cpu_tab = createTab(tabview, "CPU");
        auto* storage_tab = createTab(tabview, "Storage");
        auto* tasks_tab = createTab(tabview, "Tasks");
        auto* devices_tab = createTab(tabview, "Devices");
        auto* about_tab = createTab(tabview, "About");

        // Memory tab content
        internalMemBar = createMemoryBar(memory_tab, "Internal");

        hasExternalMem = getSpiTotal() > 0;
        if (hasExternalMem) {
            externalMemBar = createMemoryBar(memory_tab, "External");
        }

        // PSRAM tab content (only if PSRAM exists)
        if (hasExternalMem) {
            psramContainer = lv_obj_create(psram_tab);
            lv_obj_set_size(psramContainer, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(psramContainer, 8, LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(psramContainer, 0, LV_STATE_DEFAULT);
            lv_obj_set_flex_flow(psramContainer, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_bg_opa(psramContainer, 0, LV_STATE_DEFAULT);
        }

#ifdef ESP_PLATFORM
        // Storage tab content
        uint64_t storage_total = 0;
        uint64_t storage_free = 0;

        hasDataStorage = (esp_vfs_fat_info(file::MOUNT_POINT_DATA, &storage_total, &storage_free) == ESP_OK);
        if (hasDataStorage) {
            dataStorageBar = createMemoryBar(storage_tab, file::MOUNT_POINT_DATA);
        }

        const auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
        for (const auto& sdcard : sdcard_devices) {
            if (sdcard->isMounted() && esp_vfs_fat_info(sdcard->getMountPath().c_str(), &storage_total, &storage_free) == ESP_OK) {
                hasSdcardStorage = true;
                sdcardStorageBar = createMemoryBar(storage_tab, sdcard->getMountPath().c_str());
                break;  // Only show first SD card
            }
        }

        if (config::SHOW_SYSTEM_PARTITION) {
            hasSystemStorage = (esp_vfs_fat_info(file::MOUNT_POINT_SYSTEM, &storage_total, &storage_free) == ESP_OK);
            if (hasSystemStorage) {
                systemStorageBar = createMemoryBar(storage_tab, file::MOUNT_POINT_SYSTEM);
            }
        }
#endif

#if configUSE_TRACE_FACILITY
        // CPU tab - summary at top
        cpuSummaryLabel = lv_label_create(cpu_tab);
        lv_label_set_text(cpuSummaryLabel, "Overall CPU Usage: --.-%");
        lv_obj_set_style_text_font(cpuSummaryLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_bottom(cpuSummaryLabel, 4, 0);
        
        taskCountLabel = lv_label_create(cpu_tab);
        lv_label_set_text(taskCountLabel, "Active Tasks: --.-%");
        
        uptimeLabel = lv_label_create(cpu_tab);
        lv_label_set_text(uptimeLabel, "System Uptime: --.-%");
        lv_obj_set_style_pad_bottom(uptimeLabel, 8, 0);
        
        // CPU tab - container for task list (dynamic updates)
        cpuContainer = lv_obj_create(cpu_tab);
        lv_obj_set_size(cpuContainer, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(cpuContainer, 8, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(cpuContainer, 0, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(cpuContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(cpuContainer, 0, LV_STATE_DEFAULT);

        // Tasks tab - container for dynamic updates
        tasksContainer = lv_obj_create(tasks_tab);
        lv_obj_set_size(tasksContainer, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(tasksContainer, 8, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(tasksContainer, 0, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(tasksContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(tasksContainer, 0, LV_STATE_DEFAULT);
#endif

        addDevices(devices_tab);

        // Build info
        auto* tactility_version = lv_label_create(about_tab);
        lv_label_set_text_fmt(tactility_version, "Tactility v%s", TT_VERSION);
#ifdef ESP_PLATFORM
        auto* esp_idf_version = lv_label_create(about_tab);
        lv_label_set_text_fmt(esp_idf_version, "ESP-IDF v%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
#endif

        // Initial updates
        updateMemory();
        updateStorage();  // Storage: one-time update on show (doesn't change frequently)
        updateTasks();
        updatePsram();    // PSRAM: detailed breakdown

        // Start timers (only run while app is visible, stopped in onHide)
        memoryTimer.start();   // Memory & PSRAM: every 10s
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
    .appIcon = LVGL_SYMBOL_MONITORING,
    .appCategory = Category::System,
    .createApp = create<SystemInfoApp>
};

} // namespace
