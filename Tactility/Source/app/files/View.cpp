#include <Tactility/app/files/View.h>
#include <Tactility/app/files/SupportedFiles.h>

#include <Tactility/LogMessages.h>
#include <Tactility/Logger.h>
#include <Tactility/StringUtils.h>
#include <Tactility/Tactility.h>
#include <Tactility/app/ElfApp.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/imageviewer/ImageViewer.h>
#include <Tactility/app/inputdialog/InputDialog.h>
#include <Tactility/app/notes/Notes.h>
#include <tactility/check.h>
#include <Tactility/file/File.h>
#include <Tactility/kernel/Platform.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include <Tactility/service/loader/Loader.h>
#endif

namespace tt::app::files {

static const auto LOGGER = Logger("Files");

// region Callbacks

static void dirEntryListScrollBeginCallback(lv_event_t* event) {
    auto* view = static_cast<files::View*>(lv_event_get_user_data(event));
    view->onDirEntryListScrollBegin();
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

// endregion

void View::viewFile(const std::string& path, const std::string& filename) {
    std::string file_path = path + "/" + filename;

    // For PC we need to make the path relative to the current work directory,
    // because that's how LVGL maps its 'drive letter' to the file system.
    std::string processed_filepath;
    if (kernel::getPlatform() == kernel::PlatformSimulator) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            LOGGER.error("Failed to get current working directory");
            return;
        }
        if (!file_path.starts_with(cwd)) {
            LOGGER.error("Can only work with files in working directory {}", cwd);
            return;
        }
        processed_filepath = file_path.substr(strlen(cwd));
    } else {
        processed_filepath = file_path;
    }

    LOGGER.info("Clicked {}", file_path);

    if (isSupportedAppFile(filename)) {
#ifdef ESP_PLATFORM
        // install(filename);
        auto message = std::format("Do you want to install {}?", filename);
        installAppPath = processed_filepath;
        auto choices = std::vector { "Yes", "No" };
        installAppLaunchId = alertdialog::start("Install?", message, choices);
#endif
    } else if (isSupportedImageFile(filename)) {
        imageviewer::start(processed_filepath);
    } else if (isSupportedTextFile(filename)) {
        if (kernel::getPlatform() == kernel::PlatformEsp) {
            notes::start(processed_filepath);
        } else {
            // Remove forward slash, because we need a relative path
            notes::start(processed_filepath.substr(1));
        }
    } else {
        LOGGER.warn("Opening files of this type is not supported");
    }

    onNavigate();
}

void View::onDirEntryPressed(uint32_t index) {
    dirent dir_entry;
    if (state->getDirent(index, dir_entry)) {
        LOGGER.info("Pressed {} {}", dir_entry.d_name, dir_entry.d_type);
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
                LOGGER.warn("opening links is not supported");
                break;
            case TT_DT_REG:
                viewFile(state->getCurrentPath(), dir_entry.d_name);
                onNavigate();
                break;
            default:
                // Assume it's a file
                // TODO: Find a better way to identify a file
                viewFile(state->getCurrentPath(), dir_entry.d_name);
                onNavigate();
                break;
        }
    }
}

void View::onDirEntryLongPressed(int32_t index) {
    dirent dir_entry;
    if (state->getDirent(index, dir_entry)) {
        LOGGER.info("Pressed {} {}", dir_entry.d_name, dir_entry.d_type);
        state->setSelectedChildEntry(dir_entry.d_name);
        using namespace file;
        switch (dir_entry.d_type) {
            case TT_DT_DIR:
            case TT_DT_CHR:
                showActionsForDirectory();
                break;
            case TT_DT_LNK:
                LOGGER.warn("Opening links is not supported");
                break;
            case TT_DT_REG:
                showActionsForFile();
                break;
            default:
                // Assume it's a file
                // TODO: Find a better way to identify a file
                showActionsForFile();
                break;
        }
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

void View::onNavigateUpPressed() {
    if (state->getCurrentPath() != "/") {
        LOGGER.info("Navigating upwards");
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
    LOGGER.info("Pending rename {}", entry_name);
    state->setPendingAction(State::ActionRename);
    inputdialog::start("Rename", "", entry_name);
}

void View::onDeletePressed() {
    std::string file_path = state->getSelectedChildPath();
    LOGGER.info("Pending delete {}", file_path);
    state->setPendingAction(State::ActionDelete);
    std::string message = "Do you want to delete this?\n" + file_path;
    const std::vector<std::string> choices = { "Yes", "No" };
    alertdialog::start("Are you sure?", message, choices);
}

void View::onNewFilePressed() {
    LOGGER.info("Creating new file");
    state->setPendingAction(State::ActionCreateFile);
    inputdialog::start("New File", "Enter filename:", "");
}

void View::onNewFolderPressed() {
    LOGGER.info("Creating new folder");
    state->setPendingAction(State::ActionCreateFolder);
    inputdialog::start("New Folder", "Enter folder name:", "");
}

void View::showActionsForDirectory() {
    lv_obj_clean(action_list);

    auto* rename_button = lv_list_add_button(action_list, LV_SYMBOL_EDIT, "Rename");
    lv_obj_add_event_cb(rename_button, onRenamePressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* delete_button = lv_list_add_button(action_list, LV_SYMBOL_TRASH, "Delete");
    lv_obj_add_event_cb(delete_button, onDeletePressedCallback, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_remove_flag(action_list, LV_OBJ_FLAG_HIDDEN);
}

void View::showActionsForFile() {
    lv_obj_clean(action_list);

    auto* rename_button = lv_list_add_button(action_list, LV_SYMBOL_EDIT, "Rename");
    lv_obj_add_event_cb(rename_button, onRenamePressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* delete_button = lv_list_add_button(action_list, LV_SYMBOL_TRASH, "Delete");
    lv_obj_add_event_cb(delete_button, onDeletePressedCallback, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_remove_flag(action_list, LV_OBJ_FLAG_HIDDEN);
}

void View::update() {
    auto scoped_lockable = lvgl::getSyncLock()->asScopedLock();
    if (scoped_lockable.lock(lvgl::defaultLockTime)) {
        lv_obj_clean(dir_entry_list);

        state->withEntries([this](const std::vector<dirent>& entries) {
            for (auto entry : entries) {
                LOGGER.debug("Entry: {} {}", entry.d_name, entry.d_type);
                createDirEntryWidget(dir_entry_list, entry);
            }
        });

        if (state->getCurrentPath() == "/") {
            lv_obj_add_flag(navigate_up_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(navigate_up_button, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "lvgl");
    }
}

void View::init(const AppContext& appContext, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl::toolbar_create(parent, appContext);
    navigate_up_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_UP, &onNavigateUpPressedCallback, this);
    new_file_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_FILE, &onNewFilePressedCallback, this);
    new_folder_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_DIRECTORY, &onNewFolderPressedCallback, this);

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
    auto scoped_lockable = lvgl::getSyncLock()->asScopedLock();
    if (scoped_lockable.lock(lvgl::defaultLockTime)) {
        lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::onNavigate() {
    auto scoped_lockable = lvgl::getSyncLock()->asScopedLock();
    if (scoped_lockable.lock(lvgl::defaultLockTime)) {
        lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::onResult(LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) {
    if (result != Result::Ok || bundle == nullptr) {
        return;
    }

    if (
        launchId == installAppLaunchId &&
        result == Result::Ok &&
        alertdialog::getResultIndex(*bundle) == 0
    ) {
        install(installAppPath);
        return;
    }

    std::string filepath = state->getSelectedChildPath();
    LOGGER.info("Result for {}", filepath);

    switch (state->getPendingAction()) {
        case State::ActionDelete: {
            if (alertdialog::getResultIndex(*bundle) == 0) {
                if (file::isDirectory(filepath)) {
                    if (!file::deleteRecursively(filepath)) {
                        LOGGER.warn("Failed to delete {}", filepath);
                    }
                } else if (file::isFile(filepath)) {
                    auto lock = file::getLock(filepath);
                    lock->lock();
                    if (remove(filepath.c_str()) <= 0) {
                        LOGGER.warn("Failed to delete {}", filepath);
                    }
                    lock->unlock();
                }

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionRename: {
            auto new_name = inputdialog::getResult(*bundle);
            if (!new_name.empty() && new_name != state->getSelectedChildEntry()) {
                auto lock = file::getLock(filepath);
                lock->lock();
                std::string rename_to = file::getChildPath(state->getCurrentPath(), new_name);
                if (rename(filepath.c_str(), rename_to.c_str())) {
                    LOGGER.info("Renamed \"{}\" to \"{}\"", filepath, rename_to);
                } else {
                    LOGGER.error("Failed to rename \"{}\" to \"{}\"", filepath, rename_to);
                }
                lock->unlock();

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionCreateFile: {
            auto filename = inputdialog::getResult(*bundle);
            if (!filename.empty()) {
                std::string new_file_path = file::getChildPath(state->getCurrentPath(), filename);

                auto lock = file::getLock(new_file_path);
                lock->lock();

                struct stat st;
                if (stat(new_file_path.c_str(), &st) == 0) {
                    LOGGER.warn("File already exists: \"{}\"", new_file_path);
                    lock->unlock();
                    break;
                }

                FILE* new_file = fopen(new_file_path.c_str(), "w");
                if (new_file) {
                    fclose(new_file);
                    LOGGER.info("Created file \"{}\"", new_file_path);
                } else {
                    LOGGER.error("Failed to create file \"{}\"", new_file_path);
                }
                lock->unlock();

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionCreateFolder: {
            auto foldername = inputdialog::getResult(*bundle);
            if (!foldername.empty()) {
                std::string new_folder_path = file::getChildPath(state->getCurrentPath(), foldername);

                auto lock = file::getLock(new_folder_path);
                lock->lock();

                struct stat st;
                if (stat(new_folder_path.c_str(), &st) == 0) {
                    LOGGER.warn("Folder already exists: \"{}\"", new_folder_path);
                    lock->unlock();
                    break;
                }

                if (mkdir(new_folder_path.c_str(), 0755) == 0) {
                    LOGGER.info("Created folder \"{}\"", new_folder_path);
                } else {
                    LOGGER.error("Failed to create folder \"{}\"", new_folder_path);
                }
                lock->unlock();

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        default:
            break;
    }
}

}
