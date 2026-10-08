#pragma once

#include "./State.h"

#include <Tactility/app/TileGrid.h>

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

    TileGrid grid;

    lv_obj_t* overlay = nullptr;
    lv_obj_t* navigate_up_button = nullptr;
    lv_obj_t* overflow_button = nullptr;

    std::string installAppPath = { 0 };
    uint32_t installDialogId = 0;

    static std::vector<TileGridItem> collectItems(void* userData);
    static const char* resolveIcon(const TileGridItem& item, void* userData);
    static void onTileClicked(const TileGridItem& item, void* userData);
    static void onTileLongPressed(const TileGridItem& item, void* userData);
    static void onTileKey(const TileGridItem& item, uint32_t key, void* userData);
    static void onPageChanged(void* userData);

    lv_obj_t* addOverlayButton(const char* icon, lv_event_cb_t callback);
    void showOverlay(lv_obj_t* anchorTile);
    void hideOverlay();
    void showActionsForDirectory(lv_obj_t* tile);
    void showActionsForFile(lv_obj_t* tile);
    void showActionsForMountPoint(lv_obj_t* tile);
    void addCommonFileActions();

    void viewFile(const std::string&path, const std::string&filename);
    void runFile(const std::string& file_path);
    void onNavigate();

public:

    View(const std::shared_ptr<State>& state, TaskEventGroup* eventGroup);

    void init(uint32_t appInstanceId, lv_obj_t* parent);
    /**
     * Shows the current entries of the state.
     * @param[in] firstPage true after navigating to another directory, false to stay on the current page
     */
    void update(bool firstPage);

    void onBackPressed();
    void onNavigateUpPressed();
    void onOverflowPressed();
    void onDirEntryPressed(const std::string& name);
    void onDirEntryLongPressed(const std::string& name);
    void onDirEntryKeyPressed(const std::string& name, uint32_t key);
    void onRenamePressed();
    void onDeletePressed();
    void onNewFilePressed();
    void onNewFolderPressed();
    void onCopyPressed();
    void onCutPressed();
    void onPastePressed();
    void onEjectPressed();
    void onRunPressed();
    void onOverlayKey(uint32_t key);
    void onResult(uint32_t launchId, int32_t result);

private:

    bool findDirent(const std::string& name, dirent& out_entry);
    void doPaste(const std::string& src, bool is_cut, const std::string& dst);
};

}
