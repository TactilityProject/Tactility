#pragma once

#include "./State.h"

#include <app/stream.h>
#include <tactility/concurrent/task_event_group.h>

#include <cstdint>
#include <lvgl.h>
#include <memory>

namespace tt::app::files {

class View final {
    std::shared_ptr<State> state;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t appInstanceId = 0;

    AppStream inputDialogStream {};
    uint8_t inputDialogBuffer[256] {};

    size_t current_start_index = 0;
    size_t last_loaded_index = 0;
    const size_t MAX_BATCH = 50;

    lv_obj_t* dir_entry_list = nullptr;
    lv_obj_t* action_list = nullptr;
    lv_obj_t* navigate_up_button = nullptr;
    lv_obj_t* new_file_button = nullptr;
    lv_obj_t* new_folder_button = nullptr;
    lv_obj_t* paste_button = nullptr;

    std::string installAppPath = { 0 };
    uint32_t installDialogId = 0;

    void showActions();
    void showActionsForDirectory();
    void showActionsForFile();
    void showActionsForMountPoint();
    void addCommonFileActions();

    void viewFile(const std::string&path, const std::string&filename);
    void runFile(const std::string& file_path);
    void createDirEntryWidget(lv_obj_t* parent, dirent& dir_entry);
    void onNavigate();

public:

    View(const std::shared_ptr<State>& state, TaskEventGroup* eventGroup) : state(state), eventGroup(eventGroup) {}

    void init(uint32_t appInstanceId, lv_obj_t* parent);
    void update(size_t start_index = 0);

    void onBackPressed();
    void onNavigateUpPressed();
    void onDirEntryPressed(uint32_t index);
    void onDirEntryLongPressed(int32_t index);
    void onDirEntryKeyPressed(int32_t index, uint32_t key);
    void onRenamePressed();
    void onDeletePressed();
    void onNewFilePressed();
    void onNewFolderPressed();
    void onCopyPressed();
    void onCutPressed();
    void onPastePressed();
    void onEjectPressed();
    void onRunPressed();
    void onDirEntryListScrollBegin();
    void onResult(uint32_t launchId, int32_t result);
    void deinit();

private:

    bool resolveDirentFromListIndex(int32_t list_index, dirent& out_entry);
    void doPaste(const std::string& src, bool is_cut, const std::string& dst);
};

}
