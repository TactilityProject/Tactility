#include <Tactility/app/fileselection/View.h>

#include <Tactility/LogMessages.h>
#include <Tactility/Logger.h>
#include <Tactility/StringUtils.h>
#include <Tactility/Tactility.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <tactility/check.h>
#include <Tactility/file/File.h>
#include <Tactility/kernel/Platform.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>

#include <cstring>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include <Tactility/service/loader/Loader.h>
#endif

namespace tt::app::fileselection {

const static Logger LOGGER = Logger("FileSelection");

// region Callbacks

static void onDirEntryPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto index = lv_obj_get_index(button);
    view->onDirEntryPressed(index);
}

static void onNavigateUpPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNavigateUpPressed();
}

// endregion

void View::onTapFile(const std::string& path, const std::string& filename) {
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

    LOGGER.info("Clicked {}", processed_filepath);

    lv_textarea_set_text(path_textarea, processed_filepath.c_str());
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
                lv_textarea_set_text(path_textarea, state->getCurrentPathWithTrailingSlash().c_str());
                update();
                break;
            case TT_DT_LNK:
                LOGGER.warn("Opening links is not supported");
                break;
            case TT_DT_REG:
                onTapFile(state->getCurrentPath(), dir_entry.d_name);
                break;
            default:
                // Assume it's a file
                // TODO: Find a better way to identify a file
                onTapFile(state->getCurrentPath(), dir_entry.d_name);
                break;
        }
    }
}

void View::onSelectButtonPressed(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    const char* path = lv_textarea_get_text(view->path_textarea);
    if (path == nullptr || strlen(path) == 0) {
        LOGGER.warn("Select pressed, but not path found in textarea");
        return;
    }

    view->onFileSelected(std::string(path));
}

static bool isSelectableFilePath(const char* path) {
    if (path == nullptr) {
        return false;
    }

    auto len = strlen(path);
    if (len == 0) {
        return false;
    }

    return path[len - 1] != '/';
}

void View::onPathTextChanged(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    const char* path = lv_textarea_get_text(view->path_textarea);
    if (isSelectableFilePath(path)) {
        lv_obj_remove_flag(view->select_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(view->select_button, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::createDirEntryWidget(lv_obj_t* list, dirent& dir_entry) {
    check(list);
    const char* symbol;
    if (dir_entry.d_type == file::TT_DT_DIR || dir_entry.d_type == file::TT_DT_CHR) {
        symbol = LV_SYMBOL_DIRECTORY;
    } else {
        symbol = LV_SYMBOL_FILE;
    }
    lv_obj_t* button = lv_list_add_button(list, symbol, dir_entry.d_name);
    lv_obj_add_event_cb(button, &onDirEntryPressedCallback, LV_EVENT_SHORT_CLICKED, this);
}

void View::onNavigateUpPressed() {
    if (state->getCurrentPath() != "/") {
        LOGGER.info("Navigating upwards");
        std::string new_absolute_path;
        if (string::getPathParent(state->getCurrentPath(), new_absolute_path)) {
            state->setEntriesForPath(new_absolute_path);
        }
        if (new_absolute_path.length() > 1) {
            lv_textarea_set_text(path_textarea, (new_absolute_path + "/").c_str());
        } else {
            lv_textarea_set_text(path_textarea, new_absolute_path.c_str());
        }
        update();
    }
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

void View::init(lv_obj_t* parent, Mode mode) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl::toolbar_create(parent, "Select File");
    navigate_up_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_UP, &onNavigateUpPressedCallback, this);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);

    dir_entry_list = lv_list_create(wrapper);
    lv_obj_set_height(dir_entry_list, LV_PCT(100));
    lv_obj_set_flex_grow(dir_entry_list, 1);

    auto* bottom_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(bottom_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(bottom_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bottom_wrapper, 0, 0);
    lv_obj_set_style_pad_all(bottom_wrapper, 0, 0);

    path_textarea = lv_textarea_create(bottom_wrapper);
    lv_textarea_set_one_line(path_textarea, true);
    lv_obj_set_flex_grow(path_textarea, 1);
    lv_obj_add_event_cb(path_textarea, onPathTextChanged, LV_EVENT_VALUE_CHANGED, this);

    select_button = lv_button_create(bottom_wrapper);
    auto* select_button_label = lv_label_create(select_button);
    lv_label_set_text(select_button_label, "Select");
    lv_obj_add_event_cb(select_button, onSelectButtonPressed, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_flag(select_button, LV_OBJ_FLAG_HIDDEN);

    update();
}

}
