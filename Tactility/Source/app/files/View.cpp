#include <app/event.h>
#include <app/execute.h>
#include <app/install.h>
#include <app/start.h>
#include <app/stream.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/insets.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/toolbar.h>

#include <Tactility/app/files/SupportedFiles.h>
#include <Tactility/app/files/View.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/imageviewer/ImageViewer.h>
#include <Tactility/app/inputdialog/InputDialog.h>
#include <Tactility/app/notes/Notes.h>
#include <Tactility/file/File.h>
#include <Tactility/StringUtils.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/log.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <unistd.h>

namespace tt::app::files {

constexpr auto* TAG = "Files";

// region Callbacks

static void onBackPressedCallback(lv_event_t* event) {
    auto* view = static_cast<files::View*>(lv_event_get_user_data(event));
    view->onBackPressed();
}

static void onRenamePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onRenamePressed();
}

static void onDeletePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onDeletePressed();
}

static void onNavigateUpPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNavigateUpPressed();
}

static void onOverflowPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onOverflowPressed();
}

static void onPageButtonPressedCallback(lv_event_t*) {
    // TileGrid::setPageButtons() makes the page buttons change the page
}

static void onNewFilePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNewFilePressed();
}

static void onNewFolderPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNewFolderPressed();
}

static void onCopyPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onCopyPressed();
}

static void onCutPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onCutPressed();
}

static void onEjectPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onEjectPressed();
}

static void onPastePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onPastePressed();
}

static void onRunPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onRunPressed();
}

static void onOverlayKeyCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onOverlayKey(lv_event_get_key(event));
}

// endregion

// region File helpers

static bool isExecutablePath(const std::string& path) {
    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(path.c_str()) };
    return app_is_executable(location);
}

static bool isUsbMountPoint(const char* name) {
    return strncmp(name, "usb", 3) == 0 && isdigit((unsigned char)name[3]);
}

// Returns nullptr for the files that View::resolveIcon() checks, as that reads the file
static const char* getIcon(const dirent& entry, bool isRoot) {
    if (entry.d_type == file::TT_DT_DIR || entry.d_type == file::TT_DT_CHR) {
        if (isRoot && strncmp(entry.d_name, "sd", 2) == 0) {
            return LVGL_ICON_SHARED_SD_CARD;
        } else if (isRoot && isUsbMountPoint(entry.d_name)) {
            return LVGL_ICON_SHARED_USB;
        } else {
            return LVGL_ICON_SHARED_FOLDER;
        }
    } else if (entry.d_type == file::TT_DT_LNK) {
        return LVGL_ICON_SHARED_LINK;
    } else if (isSupportedImageFile(entry.d_name)) {
        return LVGL_ICON_SHARED_IMAGE;
    } else if (isSupportedTextFile(entry.d_name)) {
        return LVGL_ICON_SHARED_DESCRIPTION;
    } else if (isSupportedAppFile(entry.d_name)) {
        return LVGL_ICON_SHARED_DEPLOYED_CODE;
    } else {
        return nullptr;
    }
}

static bool copyFileContents(const std::string& src, const std::string& dst) {
    FILE* in = fopen(src.c_str(), "rb");
    if (in == nullptr) {
        return false;
    }
    FILE* out = fopen(dst.c_str(), "wb");
    if (out == nullptr) {
        fclose(in);
        return false;
    }
    uint8_t buf[512];
    bool success = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            success = false;
            break;
        }
    }
    if (ferror(in)) {
        success = false;
    }
    fclose(in);
    if (fclose(out) != 0) {
        success = false;
    }
    if (!success) {
        remove(dst.c_str());
    }
    return success;
}

static bool copyRecursive(const std::string& src, const std::string& dst) {
    if (file::isDirectory(src)) {
        if (!file::findOrCreateDirectory(dst, 0755)) {
            return false;
        }

        DIR* dir = opendir(src.c_str());
        if (!dir) {
            file::deleteRecursively(dst);
            return false;
        }

        bool success = true;
        while (success) {
            dirent* entry = readdir(dir);
            if (!entry) break;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            std::string name = entry->d_name; // copy before releasing lock

            success = copyRecursive(file::getChildPath(src, name), file::getChildPath(dst, name));
        }
        closedir(dir);

        if (!success) {
            file::deleteRecursively(dst);
        }
        return success;
    } else {
        return copyFileContents(src, dst);
    }
}

// endregion

void View::viewFile(const std::string& path, const std::string& filename) {
    std::string file_path = path + "/" + filename;
    LOG_I(TAG, "Clicked %s", file_path.c_str());

    if (isSupportedAppFile(filename)) {
        // install(filename);
        auto message = std::format("Do you want to install {}?", filename);
        installAppPath = file_path;
        auto choices = std::vector<std::string> {"Yes", "No"};
        installDialogId = alertdialog::start(appInstanceId, "Install?", message, choices);
    } else if (isSupportedImageFile(filename)) {
        imageviewer::start(file_path);
    } else if (isSupportedTextFile(filename)) {
        notes::start(file_path);
    } else if (isExecutablePath(file_path)) {
        runFile(file_path);
    } else {
        LOG_W(TAG, "Opening files of this type is not supported");
    }

    onNavigate();
}

void View::runFile(const std::string& file_path) {
    LOG_I(TAG, "Running %s", file_path.c_str());

    if (!isExecutablePath(file_path)) {
        LOG_W(TAG, "Not executable: %s", file_path.c_str());
        alertdialog::start(appInstanceId, "Run failed", "\"" + file::getLastPathSegment(file_path) + "\" is not an executable.");
        return;
    }

    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(file_path.c_str()) };
    AppInstanceId instance_id = 0;
    AppStartContext context = app_start_context_for_location(location);
    if (app_start_with_context(&context, &instance_id) != ERROR_NONE) {
        LOG_W(TAG, "Failed to run %s", file_path.c_str());
        alertdialog::start(appInstanceId, "Run failed", "Could not run \"" + file::getLastPathSegment(file_path) + "\".");
    }
}

View::View(const std::shared_ptr<State>& state, TaskEventGroup* eventGroup) :
    state(state),
    eventGroup(eventGroup),
    grid(TileGrid::Callbacks {
        .collect = collectItems,
        .onClicked = onTileClicked,
        .onLongPressed = onTileLongPressed,
        .onKey = onTileKey,
        .resolveIcon = resolveIcon,
        .onPageChanged = onPageChanged,
        .userData = this
    }) {}

std::vector<TileGridItem> View::collectItems(void* userData) {
    auto* view = static_cast<View*>(userData);
    const bool is_root = (view->state->getCurrentPath() == "/");
    std::vector<TileGridItem> items;
    view->state->withEntries([&](const std::vector<dirent>& entries) {
        items.reserve(entries.size());
        for (const auto& entry : entries) {
            items.push_back({ entry.d_name, entry.d_name, getIcon(entry, is_root), false });
        }
    });
    return items;
}

const char* View::resolveIcon(const TileGridItem& item, void* userData) {
    auto* view = static_cast<View*>(userData);
    const std::string path = file::getChildPath(view->state->getCurrentPath(), item.id);
    return isExecutablePath(path) ? LVGL_ICON_SHARED_PLAY_ARROW : LVGL_ICON_SHARED_DRAFT;
}

void View::onTileClicked(const TileGridItem& item, void* userData) {
    static_cast<View*>(userData)->onDirEntryPressed(item.id);
}

void View::onTileLongPressed(const TileGridItem& item, void* userData) {
    static_cast<View*>(userData)->onDirEntryLongPressed(item.id);
}

void View::onTileKey(const TileGridItem& item, uint32_t key, void* userData) {
    static_cast<View*>(userData)->onDirEntryKeyPressed(item.id, key);
}

void View::onPageChanged(void* userData) {
    static_cast<View*>(userData)->hideOverlay();
}

bool View::findDirent(const std::string& name, dirent& out_entry) {
    bool found = false;
    state->withEntries([&](const std::vector<dirent>& entries) {
        const auto it = std::ranges::find_if(entries, [&](const dirent& entry) { return name == entry.d_name; });
        if (it != entries.end()) {
            out_entry = *it;
            found = true;
        }
    });
    return found;
}

void View::onDirEntryPressed(const std::string& name) {
    // A tap only closes the overlay, so it can't open an entry by accident
    if (!lv_obj_is_hidden(overlay)) {
        hideOverlay();
        return;
    }

    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }

    LOG_I(TAG, "Pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);
    state->setSelectedChildEntry(dir_entry.d_name);

    using namespace tt::file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR:
            state->setEntriesForChildPath(dir_entry.d_name);
            onNavigate();
            update(true);
            break;

        case TT_DT_LNK:
            LOG_W(TAG, "opening links is not supported");
            break;

        default:
            viewFile(state->getCurrentPath(), dir_entry.d_name);
            onNavigate();
            break;
    }
}

void View::onDirEntryLongPressed(const std::string& name) {
    // Deselects the tile of an overlay that is still open
    hideOverlay();

    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }

    LOG_I(TAG, "Long-pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);
    state->setSelectedChildEntry(dir_entry.d_name);
    lv_obj_t* tile = grid.findTile(name);

    if (state->getCurrentPath() == "/") {
        // At root, only USB mount points support actions (eject).
        // Other root-level entries intentionally have no context actions.
        if (isUsbMountPoint(dir_entry.d_name)) {
            showActionsForMountPoint(tile);
        }
        return;
    }

    using namespace file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR:
            showActionsForDirectory(tile);
            break;

        case TT_DT_LNK:
            LOG_W(TAG, "Opening links is not supported");
            break;

        default:
            showActionsForFile(tile);
            break;
    }
}

void View::onDirEntryKeyPressed(const std::string& name, uint32_t key) {
    if (key == ' ') {
        onDirEntryLongPressed(name);
        return;
    }

    // Keyboards report their delete key as either delete or backspace (e.g. Cardputer's "del" key)
    const bool is_delete = key == LV_KEY_DEL || key == LV_KEY_BACKSPACE;
    const bool is_rename = key == 'r' || key == 'R';
    if (!is_delete && !is_rename) {
        return;
    }

    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }
    // Same rules as the long-press actions: root entries are mount points, links have no actions
    if (state->getCurrentPath() == "/" || dir_entry.d_type == file::TT_DT_LNK) {
        return;
    }
    state->setSelectedChildEntry(dir_entry.d_name);
    if (is_delete) {
        onDeletePressed();
    } else {
        onRenamePressed();
    }
}

void View::onBackPressed() {
    app_event_emit_close(appInstanceId);
}

void View::onNavigateUpPressed() {
    if (state->getCurrentPath() != "/") {
        LOG_I(TAG, "Navigating upwards");
        std::string new_absolute_path;
        if (string::getPathParent(state->getCurrentPath(), new_absolute_path)) {
            state->setEntriesForPath(new_absolute_path);
        }
        onNavigate();
        update(true);
    }
}

void View::onOverflowPressed() {
    if (!lv_obj_is_hidden(overlay)) {
        hideOverlay();
        return;
    }

    lv_obj_clean(overlay);
    addOverlayButton(LVGL_ICON_SHARED_NOTE_ADD, onNewFilePressedCallback);
    addOverlayButton(LVGL_ICON_SHARED_CREATE_NEW_FOLDER, onNewFolderPressedCallback);
    if (state->hasClipboard()) {
        addOverlayButton(LVGL_ICON_SHARED_CONTENT_PASTE, onPastePressedCallback);
    }
    showOverlay(nullptr);
}

void View::onRenamePressed() {
    onNavigate();
    std::string entry_name = state->getSelectedChildEntry();
    LOG_I(TAG, "Pending rename %s", entry_name.c_str());
    state->setPendingAction(State::ActionRename);
    inputdialog::start(appInstanceId, "Rename", "", entry_name, inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::onDeletePressed() {
    onNavigate();
    std::string file_path = state->getSelectedChildPath();
    LOG_I(TAG, "Pending delete %s", file_path.c_str());
    state->setPendingAction(State::ActionDelete);
    std::string message = "Do you want to delete this?\n" + file_path;
    const std::vector<std::string> choices = {"Yes", "No"};
    alertdialog::start(appInstanceId, "Are you sure?", message, choices);
}

void View::onNewFilePressed() {
    onNavigate();
    LOG_I(TAG, "Creating new file");
    state->setPendingAction(State::ActionCreateFile);
    inputdialog::start(appInstanceId, "New File", "Enter filename:", "", inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::onNewFolderPressed() {
    onNavigate();
    LOG_I(TAG, "Creating new folder");
    state->setPendingAction(State::ActionCreateFolder);
    inputdialog::start(appInstanceId, "New Folder", "Enter folder name:", "", inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

lv_obj_t* View::addOverlayButton(const char* icon, lv_event_cb_t callback) {
    auto* button = lvgl_icon_button_create(overlay);
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, icon);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, this);
    return button;
}

void View::addCommonFileActions() {
    addOverlayButton(LVGL_ICON_SHARED_CONTENT_COPY, onCopyPressedCallback);
    addOverlayButton(LVGL_ICON_SHARED_CONTENT_CUT, onCutPressedCallback);
    if (state->hasClipboard()) {
        addOverlayButton(LVGL_ICON_SHARED_CONTENT_PASTE, onPastePressedCallback);
    }
    addOverlayButton(LVGL_ICON_SHARED_EDIT, onRenamePressedCallback);
    addOverlayButton(LVGL_ICON_SHARED_DELETE, onDeletePressedCallback);
}

// The overlay covers the bottom of the grid, or its top when it would cover the anchor tile
void View::showOverlay(lv_obj_t* anchorTile) {
    lv_obj_set_hidden(overlay, false);
    lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_update_layout(overlay);
    if (anchorTile != nullptr) {
        lv_obj_add_state(anchorTile, LV_STATE_CHECKED);
        lv_area_t tile_area;
        lv_area_t overlay_area;
        lv_obj_get_coords(anchorTile, &tile_area);
        lv_obj_get_coords(overlay, &overlay_area);
        if (tile_area.y2 > overlay_area.y1) {
            lv_obj_align_to(overlay, lv_obj_get_parent(anchorTile), LV_ALIGN_TOP_MID, 0, 0);
        }
    }

    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && lv_obj_get_child_count(overlay) > 0) {
        lv_group_focus_obj(overlay);
        lv_gridnav_set_focused(overlay, lv_obj_get_child(overlay, 0), LV_ANIM_OFF);
        // Touch shows no selection, until a key moves the focus
        lv_indev_t* indev = lv_indev_active();
        if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lvgl_focus_hide_key_selection(group);
        }
    }
}

void View::hideOverlay() {
    if (overlay == nullptr || lv_obj_is_hidden(overlay)) {
        return;
    }
    lv_obj_set_hidden(overlay, true);

    lv_obj_t* tile = grid.findTile(state->getSelectedChildEntry());
    if (tile != nullptr) {
        lv_obj_remove_state(tile, LV_STATE_CHECKED);
    }

    // Keys continue on the tile that the overlay was for
    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && lv_group_get_focused(group) == overlay) {
        if (tile != nullptr) {
            lv_group_focus_obj(lv_obj_get_parent(tile));
            lv_gridnav_set_focused(lv_obj_get_parent(tile), tile, LV_ANIM_OFF);
        }
    }
}

void View::onOverlayKey(uint32_t key) {
    if (key == LV_KEY_ESC) {
        hideOverlay();
    }
}

void View::showActionsForDirectory(lv_obj_t* tile) {
    lv_obj_clean(overlay);
    addCommonFileActions();
    showOverlay(tile);
}

void View::showActionsForFile(lv_obj_t* tile) {
    lv_obj_clean(overlay);
    if (isExecutablePath(state->getSelectedChildPath())) {
        addOverlayButton(LVGL_ICON_SHARED_PLAY_ARROW, onRunPressedCallback);
    }
    addCommonFileActions();
    showOverlay(tile);
}

void View::showActionsForMountPoint(lv_obj_t* tile) {
    lv_obj_clean(overlay);
    addOverlayButton(LVGL_ICON_SHARED_EJECT, onEjectPressedCallback);
    showOverlay(tile);
}

void View::onRunPressed() {
    std::string file_path = state->getSelectedChildPath();
    onNavigate();
    runFile(file_path);
}

void View::onEjectPressed() {
    std::string mount_path = state->getSelectedChildPath();
    LOG_I(TAG, "Ejecting %s", mount_path.c_str());

    Device* msc_dev = nullptr;
    if (device_get_first_active_by_type(&USB_HOST_MSC_TYPE, &msc_dev) != ERROR_NONE || !usb_msc_eject(msc_dev, mount_path.c_str())) {
        LOG_W(TAG, "usb_msc_eject: %s not found", mount_path.c_str());
        alertdialog::start(appInstanceId, "Eject failed", "Could not eject \"" + file::getLastPathSegment(mount_path) + "\".");
    }

    if (msc_dev) {
        device_put(msc_dev);
    }

    onNavigate();
    state->setEntriesForPath(state->getCurrentPath());
    update(false);
}

void View::update(bool firstPage) {
    if (!lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "lvgl");
        return;
    }

    const bool is_root = (state->getCurrentPath() == "/");
    lv_obj_set_hidden(navigate_up_button, is_root);
    lv_obj_set_hidden(overflow_button, is_root);

    if (firstPage) {
        grid.showFirstPage();
    }
    grid.requestRepopulate(!firstPage);

    lvgl_unlock();
}

void View::init(uint32_t appInstanceId, lv_obj_t* parent) {
    this->appInstanceId = appInstanceId;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    const bool is_root = (state->getCurrentPath() == "/");
    auto* toolbar = lvgl_toolbar_create(parent, "Files");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressedCallback, this);
    navigate_up_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_UP, &onNavigateUpPressedCallback, this);
    lv_obj_set_hidden(navigate_up_button, is_root);
    auto* previous_button = lvgl_toolbar_add_text_button_action(toolbar, "<", onPageButtonPressedCallback, this);
    auto* next_button = lvgl_toolbar_add_text_button_action(toolbar, ">", onPageButtonPressedCallback, this);
    overflow_button = lvgl_toolbar_add_text_button_action(toolbar, LVGL_ICON_SHARED_MORE_VERT, onOverflowPressedCallback, this);
    lv_obj_set_style_text_font(overflow_button, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_obj_set_hidden(overflow_button, is_root);

    grid.setSwipeNavigation(true);
    grid.createWidgets(parent);
    grid.setPageButtons(previous_button, next_button);

    // Floating, so showing it doesn't change the grid's size and page layout
    overlay = lvgl_card_create(parent);
    lv_obj_set_floating(overlay, true);
    lv_obj_set_size(overlay, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(overlay, false);
    lvgl_obj_add_edge_padding(overlay);
    // The arrow keys move between the overlay's buttons as one row
    lvgl_grid_navigation_add(overlay);
    lv_obj_add_event_cb(overlay, onOverlayKeyCallback, LV_EVENT_KEY, this);
    lv_obj_set_hidden(overlay, true);
}

void View::onNavigate() {
    hideOverlay();
}

void View::onResult(uint32_t launchId, int32_t result) {
    if (launchId == installDialogId && result == 0) {
        app_install(installAppPath.c_str());
        return;
    }

    std::string filepath = state->getSelectedChildPath();
    LOG_I(TAG, "Result for %s", filepath.c_str());

    // Text-entry result (rename/new file/new folder), read from the AppStream bound to that
    // dialog's stdout. Empty for Cancel. Other pending actions (delete/paste confirmations) never
    // bound this stream; their switch cases below only look at `result`, not `resultText`.
    bool isTextEntryAction = state->getPendingAction() == State::ActionRename ||
        state->getPendingAction() == State::ActionCreateFile ||
        state->getPendingAction() == State::ActionCreateFolder;
    std::string resultText;
    if (isTextEntryAction) {
        if (result == 0) {
            char buffer[sizeof(inputDialogBuffer)];
            size_t length = app_stream_read(&inputDialogStream, buffer, sizeof(buffer));
            resultText = std::string(buffer, length);
        }
        app_stream_unsubscribe(&inputDialogStream);
    }

    switch (state->getPendingAction()) {
        case State::ActionDelete: {
            if (result == 0) {
                if (file::isDirectory(filepath)) {
                    if (!file::deleteRecursively(filepath)) {
                        LOG_W(TAG, "Failed to delete %s", filepath.c_str());
                    }
                } else if (file::isFile(filepath)) {
                    if (remove(filepath.c_str()) != 0) {
                        LOG_W(TAG, "Failed to delete %s", filepath.c_str());
                    }
                }

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
            }
            break;
        }
        case State::ActionRename: {
            std::string new_name = resultText;
            if (!new_name.empty() && new_name != state->getSelectedChildEntry()) {
                std::string rename_to = file::getChildPath(state->getCurrentPath(), new_name);
                struct stat st;
                if (stat(rename_to.c_str(), &st) == 0) {
                    LOG_W(TAG, "Rename: destination already exists: \"%s\"", rename_to.c_str());
                    state->setPendingAction(State::ActionNone);
                    alertdialog::start(appInstanceId, "Rename failed", "\"" + new_name + "\" already exists.");
                    break;
                }
                if (rename(filepath.c_str(), rename_to.c_str()) == 0) {
                    LOG_I(TAG, "Renamed \"%s\" to \"%s\"", filepath.c_str(), rename_to.c_str());
                } else {
                    LOG_E(TAG, "Failed to rename \"%s\" to \"%s\"", filepath.c_str(), rename_to.c_str());
                }

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
            }
            break;
        }
        case State::ActionCreateFile: {
            std::string filename = resultText;
            if (!filename.empty()) {
                std::string new_file_path = file::getChildPath(state->getCurrentPath(), filename);

                // O_CREAT | O_EXCL makes creation+existence-check one atomic operation, unlike a separate stat() before fopen()
                int fd = open(new_file_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
                if (fd >= 0) {
                    FILE* new_file = fdopen(fd, "w");
                    if (new_file) {
                        fclose(new_file);
                    } else {
                        close(fd);
                    }
                    LOG_I(TAG, "Created file \"%s\"", new_file_path.c_str());
                } else if (errno == EEXIST) {
                    LOG_W(TAG, "File already exists: \"%s\"", new_file_path.c_str());
                    break;
                } else {
                    LOG_E(TAG, "Failed to create file \"%s\"", new_file_path.c_str());
                }

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
            }
            break;
        }
        case State::ActionCreateFolder: {
            std::string foldername = resultText;
            if (!foldername.empty()) {
                std::string new_folder_path = file::getChildPath(state->getCurrentPath(), foldername);

                struct stat st;
                if (stat(new_folder_path.c_str(), &st) == 0) {
                    LOG_W(TAG, "Folder already exists: \"%s\"", new_folder_path.c_str());
                    break;
                }

                if (mkdir(new_folder_path.c_str(), 0755) == 0) {
                    LOG_I(TAG, "Created folder \"%s\"", new_folder_path.c_str());
                } else {
                    LOG_E(TAG, "Failed to create folder \"%s\"", new_folder_path.c_str());
                }

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
            }
            break;
        }
        case State::ActionPaste: {
            if (result == 0) {
                auto clipboard = state->getClipboard();
                if (clipboard.has_value()) {
                    std::string dst = state->getPendingPasteDst();

                    // dst was last checked before the dialog was shown; a writer could
                    // have replaced it while the user was looking at the confirmation.
                    // Revalidate right before the destructive delete so we only ever
                    // remove the exact file the user agreed to overwrite.
                    bool dst_unchanged;
                    struct stat current_stat {};
                    dst_unchanged = (stat(dst.c_str(), &current_stat) == 0) &&
                        state->pendingPasteDstMatches(current_stat);
                    state->clearPendingPasteDstStat();

                    if (!dst_unchanged) {
                        LOG_W(TAG, "Overwrite: destination \"%s\" changed since confirmation, aborting", dst.c_str());
                        state->setPendingAction(State::ActionNone);
                        alertdialog::start(
                            appInstanceId,
                            "Overwrite aborted",
                            "\"" + file::getLastPathSegment(dst) + "\" changed while the dialog was open. Please try again."
                        );
                        break;
                    }

                    // Trade-off: dst is removed before the copy attempt. If doPaste
                    // subsequently fails (e.g. source read error, out of space), the
                    // original dst data is unrecoverable. Acceptable for an embedded
                    // file manager; a safer approach would rename dst to a temp path
                    // first and roll back on failure.
                    if (file::deleteRecursively(dst)) {
                        doPaste(clipboard->first, clipboard->second, dst);
                    } else {
                        LOG_E(TAG, "Overwrite: failed to remove existing destination: \"%s\"", dst.c_str());
                        state->setPendingAction(State::ActionNone);
                        alertdialog::start(
                            appInstanceId,
                            "Overwrite failed",
                            "Could not remove \"" + file::getLastPathSegment(dst) + "\" before overwriting."
                        );
                    }
                }
            } else {
                state->clearPendingPasteDstStat();
            }
            break;
        }
        default:
            break;
    }
}

void View::onCopyPressed() {
    std::string path = state->getSelectedChildPath();
    state->setClipboard(path, false);
    LOG_I(TAG, "Copied to clipboard: %s", path.c_str());
    onNavigate();
    update(false);
}

void View::onCutPressed() {
    std::string path = state->getSelectedChildPath();
    state->setClipboard(path, true);
    LOG_I(TAG, "Cut to clipboard: %s", path.c_str());
    onNavigate();
    update(false);
}

void View::onPastePressed() {
    onNavigate();
    auto clipboard = state->getClipboard();
    if (!clipboard.has_value()) return;

    std::string src = clipboard->first;
    bool is_cut = clipboard->second;
    std::string entry_name = file::getLastPathSegment(src);
    std::string dst = file::getChildPath(state->getCurrentPath(), entry_name);

    if (src == dst) {
        LOG_I(TAG, "Paste: source and destination are the same path, skipping");
        return;
    }

    bool dst_exists;
    struct stat dst_stat {};

    // If dst exists...
    if (stat(dst.c_str(), &dst_stat) == 0) {
        state->setPendingPasteDst(dst);
        state->setPendingPasteDstStat(dst_stat);
        state->setPendingAction(State::ActionPaste);
        const std::vector<std::string> choices = {"Overwrite", "Cancel"};
        alertdialog::start(appInstanceId, "File exists", "Overwrite \"" + entry_name + "\"?", choices);
        return;
    }

    doPaste(src, is_cut, dst);
}

void View::doPaste(const std::string& src, bool is_cut, const std::string& dst) {
    bool success = false;
    bool src_delete_failed = false;
    if (is_cut) {
        if (rename(src.c_str(), dst.c_str()) != 0) {
            // Fallback for cross-filesystem moves: copy then delete.
            // Only mark success if both halves succeed — if the source removal
            // fails we leave success=false so the clipboard is preserved and
            // the error is surfaced; the user must remove the source manually.
            if (copyRecursive(src, dst)) {
                if (file::deleteRecursively(src)) {
                    success = true;
                } else {
                    src_delete_failed = true;
                    LOG_E(TAG, "Cut: copied \"%s\" to \"%s\" but failed to remove source — manual cleanup required", src.c_str(), dst.c_str());
                }
            }
        }
    } else {
        success = copyRecursive(src, dst);
    }

    const std::string filename = file::getLastPathSegment(src);
    if (success) {
        LOG_I(TAG, "%s \"%s\" to \"%s\"", is_cut ? "Moved" : "Copied", src.c_str(), dst.c_str());
        if (is_cut) {
            state->clearClipboard();
        }
    } else if (src_delete_failed) {
        state->setPendingAction(State::ActionNone); // prevent re-trigger on dialog dismiss
        alertdialog::start(appInstanceId, "Move incomplete", "\"" + filename + "\" was copied but the original could not be removed.\nPlease delete it manually.");
    } else {
        LOG_E(TAG, "Failed to %s \"%s\" to \"%s\"", is_cut ? "move" : "copy", src.c_str(), dst.c_str());
        state->setPendingAction(State::ActionNone); // prevent re-trigger on dialog dismiss
        alertdialog::start(
            appInstanceId,
            std::string("Failed to ") + (is_cut ? "move" : "copy"),
            "\"" + filename + "\" could not be " + (is_cut ? "moved." : "copied.")
        );
    }

    state->setEntriesForPath(state->getCurrentPath());
    update(false);
}

} // namespace tt::app::files
