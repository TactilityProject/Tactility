#pragma once

#include "./State.h"

#include <Tactility/app/AppManifest.h>

#include <lvgl.h>
#include <memory>

namespace tt::app::files {

class View final {
    std::shared_ptr<State> state;
    
    size_t current_start_index = 0;
    size_t last_loaded_index = 0;
    const size_t MAX_BATCH = 50;

    lv_obj_t* dir_entry_list = nullptr;
    lv_obj_t* action_list = nullptr;
    lv_obj_t* navigate_up_button = nullptr;
    lv_obj_t* new_file_button = nullptr;
    lv_obj_t* new_folder_button = nullptr;

    std::string installAppPath = { 0 };
    LaunchId installAppLaunchId = 0;

    void showActionsForDirectory();
    void showActionsForFile();

    void viewFile(const std::string&path, const std::string&filename);
    void createDirEntryWidget(lv_obj_t* parent, dirent& dir_entry);
    void onNavigate();

public:

    explicit View(const std::shared_ptr<State>& state) : state(state) {}

    void init(const AppContext& appContext, lv_obj_t* parent);
    void update(size_t start_index = 0);

    void onNavigateUpPressed();
    void onDirEntryPressed(uint32_t index);
    void onDirEntryLongPressed(int32_t index);
    void onRenamePressed();
    void onDeletePressed();
    void onNewFilePressed();
    void onNewFolderPressed();
    void onDirEntryListScrollBegin();
    void onResult(LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle);
    void deinit(const AppContext& appContext);

private:

    bool resolveDirentFromListIndex(int32_t list_index, dirent& out_entry);
};

}
