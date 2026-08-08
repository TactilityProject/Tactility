#pragma once

#include "./State.h"
#include "./FileSelectionPrivate.h"

#include <lvgl.h>
#include <memory>

namespace tt::app::fileselection {

class View final {
    uint32_t appInstanceId;
    std::shared_ptr<State> state;

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

public:

    explicit View(uint32_t appInstanceId, const std::shared_ptr<State>& state, std::function<void(const std::string& path)> onFileSelected) :
        appInstanceId(appInstanceId),
        state(state),
        on_file_selected(std::move(onFileSelected))
    {}

    void init(lv_obj_t* parent, Mode mode);
    void update();

    void onNavigateUpPressed();
    void onDirEntryPressed(uint32_t index);
    void onFileSelected(const std::string& path) const {
        on_file_selected(path);
    }
};

}
