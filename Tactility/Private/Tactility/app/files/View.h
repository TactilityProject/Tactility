#pragma once

#include <Tactility/app/TileGrid.h>
#include <lvgl/widgets/files.h>

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
    /** True from queueing a directory read until it was shown, while the busy spinner counts it. Only used on the LVGL task. */
    bool navigationPending = false;
    /** The directory of the last queued read. Only used on the LVGL task. */
    ::std::string pendingPath;
    /** Set when the shown directory changed while another one was being read. Only used on the LVGL task. */
    bool refreshDeferred = false;
    /** Signalled on the event group when a dialog was closed, after its result was stored in dialogResult and dialogText */
    uint32_t dialogBit = 0;
    /**
     * What the dialog that is shown is for. Set before it is shown, and reset on the app task when its result was read.
     * The pending action and its paths belong to that dialog, so no other action is started meanwhile.
     */
    enum class DialogOwner { None, Install, Action, Paste, Menu };
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

    /** What the items of the menu dialog do */
    enum class MenuAction { Open, Select, DeselectAll, Copy, Cut, Paste, Rename, Delete, Eject, NewFile, NewFolder };
    /** The actions of the menu dialog that is shown, by item index. Set on the LVGL task before it is shown, read on the app task after it closed. */
    ::std::vector<MenuAction> menuActions;
    /** Set when "Select" was chosen: taps then add entries to the selection or remove them, instead of opening them */
    bool selectionMode = false;

    /** The entries of the current directory, while the widgets exist */
    lv_obj_t* files = nullptr;
    /** The page of the entries, kept while there are no widgets */
    uint32_t page = 0;

    /** Shown in the toolbar while file operations are pending */
    lv_obj_t* busy_spinner = nullptr;
    ::std::atomic<int> pendingOperations = 0;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* path_bar = nullptr;
    /** Set when keys pressed a path segment: the path bar then selects the segment of the parent of the new directory */
    bool selectParentSegment = false;
    lv_obj_t* overflow_button = nullptr;

    ::std::string installAppPath = { 0 };

    static uint32_t classifyEntry(const char* directory, const dirent* entry, void* userData);
    static void getEntryIcon(lv_obj_t* files, const dirent* entry, uint32_t kind, LvglFilesIcon* icon, void* userData);
    static void onEntryClicked(lv_obj_t* files, const dirent* entry, uint32_t kind, void* userData);
    static void onEntryLongPressed(lv_obj_t* files, const dirent* entry, uint32_t kind, void* userData);
    static void onEntryKey(lv_obj_t* files, const dirent* entry, uint32_t kind, uint32_t key, void* userData);
    static bool isEntryChecked(lv_obj_t* files, const dirent* entry, uint32_t kind, void* userData);
    static void onLoaded(lv_obj_t* files, const char* path, error_t error, void* userData);
    /** Shows a directory that was read. Runs on the LVGL task. */
    void onDirectoryLoaded(const ::std::string& path, error_t error);
    /** Whether the entry of the current directory is an executable */
    bool isExecutable(const ::std::string& name) const;
    /** Whether the entry menu offers to open the entry */
    bool canOpen(const dirent& dir_entry) const;

    /** Shows the menu dialog with @a actions. Runs on the LVGL task. */
    void showMenu(const ::std::string& title, const ::std::vector<MenuAction>& actions);
    /** Shows the actions for one entry, which is selected while the menu is shown */
    void showEntryMenu(const dirent& dir_entry);
    /** Shows the actions for the entries of the selection mode */
    void showSelectionMenu();
    /** Performs a chosen menu action. Runs on the LVGL task. */
    void runMenuAction(MenuAction action);
    /** Whether taps add entries to the selection */
    bool isSelecting() const;
    /** Adds the entry to the selection or removes it. Leaves the selection mode when nothing stays selected. */
    void toggleSelectedEntry(const dirent& dir_entry);
    /** Deselects all entries and leaves the selection mode */
    void clearSelection();
    void setTileChecked(const ::std::string& name, bool checked);
    /** Opens a directory or views a file */
    void openEntry(const dirent& dir_entry);
    /** The full paths of the selected entries, or of the selected child entry when none is selected */
    ::std::vector<::std::string> getSelectedPaths() const;

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
    static void onPathSegmentPressed(lv_obj_t* bar, const char* path, bool fromKeys, void* userData);
    static void onPathBarRebuilt(lv_obj_t* bar, void* userData);
    /**
     * Reads the directory on the file operations task, then shows it. Runs on the LVGL task.
     * @param[in] selectFirst select the first entry, or the nearest path bar segment when the directory is empty
     * @param[in] focusParentSegment then select the path bar segment of the parent of the new directory
     */
    void navigateTo(const ::std::string& path, bool firstPage, bool selectFirst, bool focusParentSegment = false);
    /** Reads the current directory again, after it changed. Runs on any task. */
    void reload();

public:

    View(const ::std::shared_ptr<State>& state, TaskEventGroup* eventGroup);
    ~View();

    /** The event group bit that is signalled when the app task has to call onPasteRequested() */
    uint32_t getPasteBit() const { return pasteBit; }
    /** Rasterizes the file type icons. Call before the widgets are created. */
    void loadIcons();

    void init(uint32_t appInstanceId, lv_obj_t* parent);
    /** Forgets the widgets, which are deleted when another window covers this one. Runs on the LVGL task with the lock held. */
    void destroyWidgets();
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
    /** The event group bit that is signalled when the app task has to call onDialogResult() */
    uint32_t getDialogBit() const { return dialogBit; }
    /** Stores the result of a dialog and signals the app task. Runs on the task of the dialog. */
    void onDialogClosed(int32_t result, const char* text);
    /** Handles the result of the dialog that was closed. Runs on the app task. */
    void onDialogResult();

private:

    bool findDirent(const ::std::string& name, dirent& out_entry) const;
    /** Handles the queued paste sources until one needs an overwrite confirmation, or all are done */
    void processPasteQueue();
    void addPasteFailure(const ::std::string& text) { pasteFailures.push_back(State::toExt(text)); }
    /** Copies or moves one source. A failure is added to pasteFailures. */
    void doPaste(const ::std::string& src, bool is_cut, const ::std::string& dst);
};

}
