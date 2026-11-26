#pragma once

#include <lvgl.h>
#include <lvgl/widgets/files.h>

#include <tactility/error.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Included last, as it makes std:: resolve to tt::std
#include "./State.h"

namespace tt::app::fileselection {

class View final {
    uint32_t appInstanceId;
    ::std::shared_ptr<State> state;
    ::std::function<void(::std::vector<::std::string> paths)> on_file_selected;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* path_bar = nullptr;
    /** Picks the selection. Only shown when the selection is valid. */
    lv_obj_t* select_button = nullptr;
    lv_obj_t* files = nullptr;
    /** Shown when the first directory could not be queued for reading */
    lv_obj_t* load_error_label = nullptr;
    /** For typing the name of a new file in the directory that is shown, only with Options::allowNewFile */
    lv_obj_t* path_textarea = nullptr;
    /** The text of the name field, kept so the field can be rebuilt and the result doesn't need the widgets */
    tt::std::string nameText;
    /** The page of the entries, kept while there are no widgets */
    uint32_t page = 0;
    /** Set when keys pressed a path segment: the path bar then selects the segment of the parent of the new directory */
    bool selectParentSegment = false;

    /** The paths that the select button returns, or none when the selection isn't valid */
    ::std::vector<::std::string> getResultPaths() const;
    /** Shows the select button when the selection is valid. @a fitPathBar rebuilds the path bar for the room that is left. */
    void updateSelectButton(bool fitPathBar = true);
    void onTapFile(const ::std::string& filePath);
    /** Reads the directory on the file operations task, then shows it */
    void navigateTo(const ::std::string& path, bool selectFirst);
    void onDirectoryLoaded(const ::std::string& path, error_t error);
    void setTileChecked(const ::std::string& name, bool checked);

    static bool filterEntry(const char* directory, const dirent* entry, void* userData);
    static bool isEntryChecked(lv_obj_t* files, const dirent* entry, uint32_t kind, void* userData);
    static void onEntryClicked(lv_obj_t* files, const dirent* entry, uint32_t kind, void* userData);
    static void onLoaded(lv_obj_t* files, const char* path, error_t error, void* userData);
    static void onPathSegmentPressed(lv_obj_t* bar, const char* path, bool fromKeys, void* userData);
    static void onPathBarRebuilt(lv_obj_t* bar, void* userData);
    static void onSelectButtonPressed(lv_event_t* event);
    static void onNameTextChanged(lv_event_t* event);
    /** Emits an async APP_EVENT_CLOSE for appInstanceId - see FileSelection.cpp's appMain() for
     * why this indirection (rather than calling app_manager_stop() here) is required. */
    static void onBackPressedCallback(lv_event_t* event);

public:

    View(uint32_t appInstanceId, const ::std::shared_ptr<State>& state, ::std::function<void(const ::std::vector<::std::string>& paths)> onFileSelected);

    void init(lv_obj_t* parent);
    /** Forgets the widgets, which are deleted when another window covers this one. Runs on the LVGL task with the lock held. */
    void destroyWidgets();

    void onFileSelected(const ::std::vector<::std::string>& paths) const {
        on_file_selected(paths);
    }
};

}
