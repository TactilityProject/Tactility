#pragma once

#include <Tactility/app/TileGrid.h>

#include <lvgl_window_manager/dialog.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>

#include <atomic>
#include <cstdint>
#include <lvgl.h>
#include <memory>
#include <string>
#include <vector>

// Included last, as it makes std:: resolve to tt::std
#include "./State.h"

namespace tt::app::files {

class View final {
    ::std::shared_ptr<State> state;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t appInstanceId = 0;
    /** Signalled on the event group when a paste is requested */
    uint32_t pasteBit = 0;
    /** Reports that a queued directory read completed, on the event group */
    FileOpsSubscription fileOpsSub {};
    /** True from queueing a directory read until it completed. Set on the LVGL task, cleared on the app task. */
    ::std::atomic<bool> navigationPending = false;
    /** Signalled on the event group when a dialog was closed, after its result was stored in dialogResult and dialogText */
    uint32_t dialogBit = 0;
    /**
     * What the dialog that is shown is for. Set before it is shown, and reset on the app task when its result was read.
     * The pending action and its paths belong to that dialog, so no other action is started meanwhile.
     */
    enum class DialogOwner { None, Install, Action, Paste };
    ::std::atomic<DialogOwner> dialogOwner = DialogOwner::None;
    /** The pressed button, or 0 for OK and 1 for Cancel in a text dialog, or WINDOW_DIALOG_DISMISSED */
    int32_t dialogResult = WINDOW_DIALOG_DISMISSED;
    ::std::string dialogText;
    /** The entry that the rename dialog is for, chosen when it was opened */
    ::std::string renameTargetPath;
    /** Whether the overwrite confirmation waits for an answer. Only used on the app task. */
    bool pasteDialogShown = false;

    /** The sources of a paste that still have to be handled, with its destination folder and mode. Only used on the app task. */
    tt::std::vector<tt::std::string> pasteQueue;
    tt::std::string pasteFolder;
    bool pasteIsCut = false;
    /** A line per source of the current paste that failed. Only used on the app task. */
    tt::std::vector<tt::std::string> pasteFailures;

    /** Rasterized file type icons, indexed by FileIcon. Freed after the widgets are gone. */
    lv_draw_buf_t* icons[9] = {};
    /** Mono icons are painted in the theme's icon colour */
    bool iconRecolor[9] = {};

    /** Set while a key selects an entry, so the keys keep going to the grid instead of the overlay */
    bool keepGridFocus = false;

    TileGrid grid;

    /** Shown in the toolbar while file operations are pending */
    lv_obj_t* busy_spinner = nullptr;
    ::std::atomic<int> pendingOperations = 0;

    lv_obj_t* overlay = nullptr;
    lv_obj_t* toolbar = nullptr;
    /** The path bar: the buttons of the path segments that fit, and an ellipsis in front when the path is cut off */
    ::std::vector<lv_obj_t*> pathBarWidgets;
    /** The path that each segment button of the path bar navigates to */
    ::std::vector<::std::string> pathBarPaths;
    /** Set when keys opened a directory, so its first entry is selected instead of nothing */
    bool selectFirstAfterNavigation = false;
    /** Set when a path segment was pressed: the path bar then selects the segment of the parent of the new directory */
    bool selectParentSegment = false;
    /** The toolbar width that the path bar was built for */
    int32_t pathBarWidth = 0;
    lv_obj_t* overflow_button = nullptr;

    ::std::string installAppPath = { 0 };

    static ::std::vector<TileGridItem> collectItems(void* userData);
    static const lv_image_dsc_t* resolveImage(const TileGridItem& item, void* userData);
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
    /** Whether actions are shown for a selection that more entries can be added to */
    bool isSelecting() const;
    /** Adds the entry to the selection or removes it, and updates the actions */
    void toggleSelectedEntry(const dirent& dir_entry);
    /** Shows the actions for the selected entries, anchored at @a tile */
    void showActionsForSelection(lv_obj_t* tile);
    /** The full paths of the selected entries, or of the selected child entry when none is selected */
    ::std::vector<::std::string> getSelectedPaths() const;
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

    void viewFile(const ::std::string&path, const ::std::string&filename);
    void runFile(const ::std::string& file_path);
    void onNavigate();
    /** Rebuilds the path bar for the current path. Runs on the LVGL task. */
    void updatePathBar();
    /** @return the path bar segment of the parent of the current directory, or the current directory when it has no pressable parent shown */
    lv_obj_t* getParentPathSegment() const;
    static void onPathSegmentPressed(lv_event_t* event);
    static void onToolbarSizeChanged(lv_event_t* event);
    static void onDeferredPathBarUpdate(void* userData);
    /** Reads the directory on the file operations task, then shows it. Runs on the LVGL task. */
    void navigateTo(const ::std::string& path, bool firstPage, bool selectFirst);

public:

    View(const ::std::shared_ptr<State>& state, TaskEventGroup* eventGroup);
    ~View();

    /** The event group bit that is signalled when the app task has to call onPasteRequested() */
    uint32_t getPasteBit() const { return pasteBit; }
    /** The event group bit that is signalled when the app task has to call onFileOpsCompleted() */
    uint32_t getFileOpsBit() const { return fileOpsSub.bit; }

    /** Shows the directories that were read by the file operations task. Runs on the app task. */
    void onFileOpsCompleted();

    /** Rasterizes the file type icons. Call before the widgets are created. */
    void loadIcons();

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
    void onOverflowPressed();
    void onDirEntryPressed(const ::std::string& name);
    void onDirEntryLongPressed(const ::std::string& name);
    void onDirEntryKeyPressed(const ::std::string& name, uint32_t key);
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
    /** The event group bit that is signalled when the app task has to call onDialogResult() */
    uint32_t getDialogBit() const { return dialogBit; }
    /** Stores the result of a dialog and signals the app task. Runs on the task of the dialog. */
    void onDialogClosed(int32_t result, const char* text);
    /** Handles the result of the dialog that was closed. Runs on the app task. */
    void onDialogResult();

private:

    bool findDirent(const ::std::string& name, dirent& out_entry);
    /** Handles the queued paste sources until one needs an overwrite confirmation, or all are done */
    void processPasteQueue();
    void addPasteFailure(const ::std::string& text) { pasteFailures.push_back(State::toExt(text)); }
    /** Copies or moves one source. A failure is added to pasteFailures. */
    void doPaste(const ::std::string& src, bool is_cut, const ::std::string& dst);
};

}
