#include <app/event.h>
#include <app/execute.h>
#include <app/install.h>
#include <app/start.h>
#include <app/stream.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <Tactility/app/files/SupportedFiles.h>
#include <Tactility/app/files/View.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/imageviewer/ImageViewer.h>
#include <Tactility/app/inputdialog/InputDialog.h>
#include <Tactility/app/notes/Notes.h>
#include <Tactility/file/File.h>
#include <Tactility/Platform.h>
#include <Tactility/StringUtils.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/log.h>

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

static void dirEntryListScrollBeginCallback(lv_event_t* event) {
    auto* view = static_cast<files::View*>(lv_event_get_user_data(event));
    view->onDirEntryListScrollBegin();
}

static void onBackPressedCallback(lv_event_t* event) {
    auto* view = static_cast<files::View*>(lv_event_get_user_data(event));
    view->onBackPressed();
}

static void onDirEntryPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto index = lv_obj_get_index(button);
    view->onDirEntryPressed(index);
}

static void onDirEntryLongPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto index = lv_obj_get_index(button);
    view->onDirEntryLongPressed(index);
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

// endregion

// region File helpers

static bool isExecutablePath(const std::string& path) {
    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(path.c_str()) };
    return app_is_executable(location);
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
        if (kernel::getPlatform() == kernel::PlatformEsp) {
            notes::start(file_path);
        } else {
            // Remove forward slash, because we need a relative path
            notes::start(file_path.substr(1));
        }
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

bool View::resolveDirentFromListIndex(int32_t list_index, dirent& out_entry) {
    const bool is_root = (state->getCurrentPath() == "/");
    const bool has_back = (!is_root && current_start_index > 0);

    if (has_back && list_index == 0) {
        return false; // Back button
    }

    const size_t adjusted_index =
        current_start_index + static_cast<size_t>(list_index) - (has_back ? 1 : 0);

    return state->getDirent(static_cast<uint32_t>(adjusted_index), out_entry);
}

void View::onDirEntryPressed(uint32_t index) {
    dirent dir_entry;
    if (!resolveDirentFromListIndex(static_cast<int32_t>(index), dir_entry)) {
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
            update();
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

void View::onDirEntryLongPressed(int32_t index) {
    dirent dir_entry;
    if (!resolveDirentFromListIndex(index, dir_entry)) {
        return;
    }

    LOG_I(TAG, "Long-pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);
    state->setSelectedChildEntry(dir_entry.d_name);

    if (state->getCurrentPath() == "/") {
        // At root, only USB mount points support actions (eject).
        // Other root-level entries intentionally have no context actions.
        const char* name = dir_entry.d_name;
        if (strncmp(name, "usb", 3) == 0 && isdigit((unsigned char)name[3])) {
            showActionsForMountPoint();
        }
        return;
    }

    using namespace file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR:
            showActionsForDirectory();
            break;

        case TT_DT_LNK:
            LOG_W(TAG, "Opening links is not supported");
            break;

        default:
            showActionsForFile();
            break;
    }
}

void View::createDirEntryWidget(lv_obj_t* list, dirent& dir_entry) {
    check(list);
    const char* symbol;
    if (dir_entry.d_type == file::TT_DT_DIR || dir_entry.d_type == file::TT_DT_CHR) {
        symbol = LV_SYMBOL_DIRECTORY;
    } else if (isSupportedImageFile(dir_entry.d_name)) {
        symbol = LV_SYMBOL_IMAGE;
    } else if (dir_entry.d_type == file::TT_DT_LNK) {
        symbol = LV_SYMBOL_LOOP;
    } else if (isExecutablePath(file::getChildPath(state->getCurrentPath(), dir_entry.d_name))) {
        symbol = LV_SYMBOL_PLAY;
    } else {
        symbol = LV_SYMBOL_FILE;
    }

    // Get file size for regular files
    std::string label_text = dir_entry.d_name;
    if (dir_entry.d_type == file::TT_DT_REG) {
        std::string file_path = file::getChildPath(state->getCurrentPath(), dir_entry.d_name);
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0) {
            // Format file size in human-readable format
            const char* size_suffix;
            double size;
            if (st.st_size < 1024) {
                size = st.st_size;
                size_suffix = " B";
            } else if (st.st_size < 1024 * 1024) {
                size = st.st_size / 1024.0;
                size_suffix = " KB";
            } else {
                size = st.st_size / (1024.0 * 1024.0);
                size_suffix = " MB";
            }

            char size_str[32];
            if (st.st_size < 1024) {
                snprintf(size_str, sizeof(size_str), " (%d%s)", (int)size, size_suffix);
            } else {
                snprintf(size_str, sizeof(size_str), " (%.1f%s)", size, size_suffix);
            }
            label_text += size_str;
        }
    }

    lv_obj_t* button = lv_list_add_button(list, symbol, label_text.c_str());
    lv_obj_add_event_cb(button, &onDirEntryPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(button, &onDirEntryLongPressedCallback, LV_EVENT_LONG_PRESSED, this);
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
        update();
    }
}

void View::onRenamePressed() {
    std::string entry_name = state->getSelectedChildEntry();
    LOG_I(TAG, "Pending rename %s", entry_name.c_str());
    state->setPendingAction(State::ActionRename);
    inputdialog::start(appInstanceId, "Rename", "", entry_name, inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::onDeletePressed() {
    std::string file_path = state->getSelectedChildPath();
    LOG_I(TAG, "Pending delete %s", file_path.c_str());
    state->setPendingAction(State::ActionDelete);
    std::string message = "Do you want to delete this?\n" + file_path;
    const std::vector<std::string> choices = {"Yes", "No"};
    alertdialog::start(appInstanceId, "Are you sure?", message, choices);
}

void View::onNewFilePressed() {
    LOG_I(TAG, "Creating new file");
    state->setPendingAction(State::ActionCreateFile);
    inputdialog::start(appInstanceId, "New File", "Enter filename:", "", inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::onNewFolderPressed() {
    LOG_I(TAG, "Creating new folder");
    state->setPendingAction(State::ActionCreateFolder);
    inputdialog::start(appInstanceId, "New Folder", "Enter folder name:", "", inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::addCommonFileActions() {
    auto* copy_button = lv_list_add_button(action_list, LV_SYMBOL_COPY, "Copy");
    lv_obj_add_event_cb(copy_button, onCopyPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* cut_button = lv_list_add_button(action_list, LV_SYMBOL_CUT, "Cut");
    lv_obj_add_event_cb(cut_button, onCutPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* rename_button = lv_list_add_button(action_list, LV_SYMBOL_EDIT, "Rename");
    lv_obj_add_event_cb(rename_button, onRenamePressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* delete_button = lv_list_add_button(action_list, LV_SYMBOL_TRASH, "Delete");
    lv_obj_add_event_cb(delete_button, onDeletePressedCallback, LV_EVENT_SHORT_CLICKED, this);
}

void View::showActions() {
    lv_obj_clean(action_list);
    addCommonFileActions();
    lv_obj_remove_flag(action_list, LV_OBJ_FLAG_HIDDEN);
}

void View::showActionsForDirectory() { showActions(); }

void View::showActionsForFile() {
    lv_obj_clean(action_list);

    if (isExecutablePath(state->getSelectedChildPath())) {
        auto* run_button = lv_list_add_button(action_list, LV_SYMBOL_PLAY, "Run");
        lv_obj_add_event_cb(run_button, onRunPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    }

    addCommonFileActions();
    lv_obj_remove_flag(action_list, LV_OBJ_FLAG_HIDDEN);
}

void View::showActionsForMountPoint() {
    lv_obj_clean(action_list);

    auto* eject_button = lv_list_add_button(action_list, LV_SYMBOL_EJECT, "Eject");
    lv_obj_add_event_cb(eject_button, onEjectPressedCallback, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_remove_flag(action_list, LV_OBJ_FLAG_HIDDEN);
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
    update();
}

void View::update(size_t start_index) {
    const bool is_root = (state->getCurrentPath() == "/");

    if (!lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "lvgl");
        return;
    }

    lv_obj_clean(dir_entry_list);

    current_start_index = start_index;

    state->withEntries([this, is_root](const std::vector<dirent>& entries) {
        size_t total_entries = entries.size();
        if (current_start_index >= total_entries) {
            current_start_index = (total_entries > MAX_BATCH)
                ? (total_entries - MAX_BATCH)
                : 0;
        }
        size_t count = 0;

        if (!is_root && current_start_index > 0) {
            auto* back_btn = lv_list_add_btn(dir_entry_list, LV_SYMBOL_LEFT, "Back");
            lv_obj_add_event_cb(back_btn, [](lv_event_t* event) {
                auto* view = static_cast<View*>(lv_event_get_user_data(event));
                size_t new_index = (view->current_start_index >= view->MAX_BATCH) ? 
                                    view->current_start_index - view->MAX_BATCH : 0;
                view->update(new_index); }, LV_EVENT_SHORT_CLICKED, this);
        }

        for (size_t i = current_start_index; i < total_entries; ++i) {
            auto entry = entries[i];

            createDirEntryWidget(dir_entry_list, entry);
            count++;

            if (count >= MAX_BATCH) {
                break;
            }
        }

        last_loaded_index = std::min(current_start_index + count, total_entries);

        if (!is_root && last_loaded_index < total_entries) {
            if (total_entries > current_start_index &&
                (total_entries - current_start_index) > MAX_BATCH) {
                auto* next_btn = lv_list_add_btn(dir_entry_list, LV_SYMBOL_RIGHT, "Next");
                lv_obj_add_event_cb(next_btn, [](lv_event_t* event) {
                    auto* view = static_cast<View*>(lv_event_get_user_data(event));
                    view->update(view->last_loaded_index); }, LV_EVENT_SHORT_CLICKED, this);
            }
        } else {
            last_loaded_index = total_entries;
        }
    });

    if (is_root) {
        lv_obj_add_flag(lv_obj_get_parent(navigate_up_button), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(lv_obj_get_parent(navigate_up_button), LV_OBJ_FLAG_HIDDEN);
    }

    if (state->hasClipboard() && !is_root) {
        lv_obj_remove_flag(lv_obj_get_parent(paste_button), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lv_obj_get_parent(paste_button), LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_unlock();
}

void View::init(uint32_t appInstanceId, lv_obj_t* parent) {
    this->appInstanceId = appInstanceId;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Files");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressedCallback, this);
    navigate_up_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_UP, &onNavigateUpPressedCallback, this);
    new_file_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_FILE, &onNewFilePressedCallback, this);
    new_folder_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_DIRECTORY, &onNewFolderPressedCallback, this);
    paste_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_PASTE, &onPastePressedCallback, this);
    lv_obj_add_flag(lv_obj_get_parent(paste_button), LV_OBJ_FLAG_HIDDEN);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);

    dir_entry_list = lv_list_create(wrapper);
    lv_obj_set_height(dir_entry_list, LV_PCT(100));
    lv_obj_set_flex_grow(dir_entry_list, 1);

    lv_obj_add_event_cb(dir_entry_list, dirEntryListScrollBeginCallback, LV_EVENT_SCROLL_BEGIN, this);

    action_list = lv_list_create(wrapper);
    lv_obj_set_height(action_list, LV_PCT(100));
    lv_obj_set_flex_grow(action_list, 1);
    lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);

    update();
}

void View::onDirEntryListScrollBegin() {
    if (lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);
        lvgl_unlock();
    }
}

void View::onNavigate() {
    if (lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);
        lvgl_unlock();
    }
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
                update();
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
                update();
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
                update();
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
                update();
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
    update();
}

void View::onCutPressed() {
    std::string path = state->getSelectedChildPath();
    state->setClipboard(path, true);
    LOG_I(TAG, "Cut to clipboard: %s", path.c_str());
    onNavigate();
    update();
}

void View::onPastePressed() {
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
    update();
}

void View::deinit() {
    lv_obj_remove_event_cb(dir_entry_list, dirEntryListScrollBeginCallback);
}

} // namespace tt::app::files
