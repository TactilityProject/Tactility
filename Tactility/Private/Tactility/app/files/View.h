#pragma once

#include "./State.h"

#include <Tactility/app/TileGrid.h>

#include <app/stream.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>

#include <atomic>
#include <cstdint>
#include <lvgl.h>
#include <memory>

namespace tt::app::files {

class View final {
    std::shared_ptr<State> state;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t appInstanceId = 0;
    /** Signalled on the event group when a paste is requested */
    uint32_t pasteBit = 0;
    /** Reports that a queued directory read completed, on the event group */
    FileOpsSubscription fileOpsSub {};
    /** True from queueing a directory read until it completed. Set on the LVGL task, cleared on the app task. */
    std::atomic<bool> navigationPending = false;
    /** The launch id of the overwrite confirmation that waits for an answer, or 0. Only used on the app task. */
    uint32_t pasteDialogId = 0;

    AppStream inputDialogStream {};
    uint8_t inputDialogBuffer[256] {};

    TileGrid grid;

    /** Shown in the toolbar while file operations are pending */
    lv_obj_t* busy_spinner = nullptr;
    std::atomic<int> pendingOperations = 0;

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

    /** Shows the busy spinner while at least one operation is pending. Runs on any task. */
    void beginOperation();
    void endOperation();
    void updateBusySpinner();

    /** Marks an operation as pending for the lifetime of the object */
    struct OperationScope {
        View& view;
        explicit OperationScope(View& view) : view(view) { view.beginOperation(); }
        ~OperationScope() { view.endOperation(); }
    };

    void viewFile(const std::string&path, const std::string&filename);
    void runFile(const std::string& file_path);
    void onNavigate();
    /** Reads the directory on the file operations task, then shows it. Runs on the LVGL task. */
    void navigateTo(const std::string& path, bool firstPage, bool selectFirst);

public:

    View(const std::shared_ptr<State>& state, TaskEventGroup* eventGroup);
    ~View();

    /** The event group bit that is signalled when the app task has to call onPasteRequested() */
    uint32_t getPasteBit() const { return pasteBit; }
    /** The event group bit that is signalled when the app task has to call onFileOpsCompleted() */
    uint32_t getFileOpsBit() const { return fileOpsSub.bit; }

    /** Shows the directories that were read by the file operations task. Runs on the app task. */
    void onFileOpsCompleted();

    void init(uint32_t appInstanceId, lv_obj_t* parent);
    /** Forgets the widgets, which are deleted when another window covers this one. Runs on the LVGL task with the lock held. */
    void destroyWidgets();
    /**
     * Shows the current entries of the state.
     * @param[in] firstPage true after navigating to another directory, false to stay on the current page
     * @param[in] selectFirst with firstPage and without a pointer device: select the first entry, or the navigate up button when empty.
     * When false the focus stays where it is.
     */
    void update(bool firstPage, bool selectFirst = true);

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
    /** Requests a paste, which the app task performs. Runs on the LVGL thread. */
    void onPastePressed();
    /** Does the file system work of a requested paste. Runs on the app task. */
    void onPasteRequested();
    void onEjectPressed();
    void onRunPressed();
    void onOverlayKey(uint32_t key);
    void onResult(uint32_t launchId, int32_t result);

private:

    bool findDirent(const std::string& name, dirent& out_entry);
    void doPaste(const std::string& src, bool is_cut, const std::string& dst);
};

}
