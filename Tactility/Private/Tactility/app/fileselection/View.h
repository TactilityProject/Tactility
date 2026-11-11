#pragma once

#include "./State.h"
#include "./FileSelectionPrivate.h"

#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>

#include <atomic>
#include <lvgl.h>
#include <memory>

namespace tt::app::fileselection {

class View final {
    uint32_t appInstanceId;
    std::shared_ptr<State> state;
    /** Reports that a queued directory read completed, on the event group */
    FileOpsSubscription fileOpsSub {};
    /** True from queueing a directory read until it completed. Set on the LVGL task, cleared on the app task. */
    std::atomic<bool> navigationPending = false;

    lv_obj_t* dir_entry_list = nullptr;
    lv_obj_t* navigate_up_button = nullptr;
    lv_obj_t* path_textarea = nullptr;
    lv_obj_t* select_button = nullptr;
    std::function<void(std::string path)> on_file_selected;

    void onTapFile(const std::string&path, const std::string&filename);
    static void onSelectButtonPressed(lv_event_t* event);
    static void onPathTextChanged(lv_event_t* event);
    /** Emits an async APP_EVENT_CLOSE for appInstanceId - see FileSelection.cpp's appMain() for
     * why this indirection (rather than calling app_manager_stop() here) is required. */
    static void onBackPressedCallback(lv_event_t* event);
    void createDirEntryWidget(lv_obj_t* parent, dirent& dir_entry);
    /** Reads the directory on the file operations task, then shows it. Runs on the LVGL task. */
    void navigateTo(const std::string& path);

public:

    View(uint32_t appInstanceId, const std::shared_ptr<State>& state, TaskEventGroup* eventGroup, std::function<void(const std::string& path)> onFileSelected);
    ~View();

    /** The event group bit that is signalled when the app task has to call onFileOpsCompleted() */
    uint32_t getFileOpsBit() const { return fileOpsSub.bit; }

    /** Shows the directories that were read by the file operations task. Runs on the app task. */
    void onFileOpsCompleted();

    void init(lv_obj_t* parent, Mode mode);
    void update();

    void onNavigateUpPressed();
    void onDirEntryPressed(uint32_t index);
    void onFileSelected(const std::string& path) const {
        on_file_selected(path);
    }
};

}
