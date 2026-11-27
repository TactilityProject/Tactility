// SPDX-License-Identifier: Apache-2.0
#include <lvgl_window_manager/dialog.h>
#include <lvgl_window_manager/system_bars.h>

#include "window_manager_internal.h"

#include <localisation/tr.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/files.h>
#include <lvgl/widgets/path_bar.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/Memory.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#include <algorithm>
#include <cctype>
#include <unistd.h>

namespace {

constexpr auto* TAG = "file_dialog";

// The paths prefer external memory
using String = tt::std::string;
using Strings = tt::std::vector<String>;

// region Paths

String lowercase(String text) {
    for (auto& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

String child_path(const String& directory, const String& name) {
    return directory.empty() || directory.back() == '/' ? directory + name : directory + "/" + name;
}

String last_segment(const String& path) {
    return path.substr(path.find_last_of('/') + 1);
}

/** Resolves a path against the working directory, without "." and ".." segments */
String absolute_path(const String& path) {
    char working_directory[FILE_MAX_PATH_STRING_LENGTH];
    const String base = getcwd(working_directory, sizeof(working_directory)) != nullptr ? working_directory : "/";
    const String joined = path.front() == '/' ? path : base + "/" + path;
    String result;
    size_t start = 0;
    while (start < joined.size()) {
        size_t end = joined.find('/', start);
        if (end == String::npos) {
            end = joined.size();
        }
        const auto segment = joined.substr(start, end - start);
        if (segment == "..") {
            result.erase(result.empty() ? 0 : result.find_last_of('/'));
        } else if (!segment.empty() && segment != ".") {
            result += "/" + segment;
        }
        start = end + 1;
    }
    return result.empty() ? "/" : result;
}

// endregion

// region Paging with the page buttons of the system bars

LvglTileGridIndicator get_indicator(lv_obj_t*) {
    if (system_bars_get_layout() != SYSTEM_BARS_LAYOUT_SIDE) {
        return LVGL_TILE_GRID_INDICATOR_BELOW;
    }
    // The dots are on the inner side of the strip
    return system_bars_get_alignment() == SYSTEM_BARS_ALIGNMENT_RIGHT ? LVGL_TILE_GRID_INDICATOR_RIGHT : LVGL_TILE_GRID_INDICATOR_LEFT;
}

bool is_vertical_swipe(lv_obj_t*) {
    return system_bars_get_layout() == SYSTEM_BARS_LAYOUT_SIDE;
}

bool ignore_swipe(lv_obj_t*) {
    // A swipe down from the top of the screen opens the system menu
    return system_bars_get_layout() == SYSTEM_BARS_LAYOUT_SIDE && system_bars_is_menu_swipe_zone_pressed();
}

void on_previous_page(void* user_data) {
    lvgl_tile_grid_previous_page(static_cast<lv_obj_t*>(user_data));
}

void on_next_page(void* user_data) {
    lvgl_tile_grid_next_page(static_cast<lv_obj_t*>(user_data));
}

// The dialog restores what the page bar did before it, so nothing is reset when the grid is deleted
void on_paging_visible(lv_obj_t* grid, bool visible) {
    // A closed dialog is hidden until it is deleted, and has given the page bar back
    for (lv_obj_t* object = grid; object != nullptr; object = lv_obj_get_parent(object)) {
        if (lv_obj_is_hidden(object)) {
            return;
        }
    }
    system_bars_set_page_button_callbacks(on_previous_page, on_next_page, grid);
    system_bars_set_page_buttons_visible(visible);
}

const LvglTileGridPaging PAGING = {
    .get_indicator = get_indicator,
    .is_vertical_swipe = is_vertical_swipe,
    .ignore_swipe = ignore_swipe,
    .on_paging_visible = on_paging_visible,
};

// endregion

/**
 * The widgets are only used with the LVGL lock held. The options don't change after construction, as the file operations
 * task reads them too.
 */
class FileDialog final : public DialogContent {

    uint32_t flags = 0;
    String initialPath;
    String initialName;
    /** Without dot and in lower case */
    Strings extensions;
    WindowFileDialogResultFn onResult;
    void* userData;

    void* handle = nullptr;
    lv_obj_t* selectButton = nullptr;
    lv_obj_t* pathBar = nullptr;
    lv_obj_t* files = nullptr;
    lv_obj_t* errorLabel = nullptr;
    lv_obj_t* nameField = nullptr;
    /** The directory that is shown, empty until one was read */
    String currentPath;
    Strings selectedPaths;
    /** Wait for the dialog task to check them */
    Strings pickedPaths;
    Strings result;
    bool selectParentSegment = false;

    bool has(uint32_t flag) const { return (flags & flag) != 0; }

    bool isSelectableFile(const String& path) const {
        if (has(WINDOW_FILE_DIALOG_NO_FILES)) {
            return false;
        }
        if (extensions.empty()) {
            return true;
        }
        const auto name = last_segment(path);
        const auto dot = name.find_last_of('.');
        return dot != String::npos && std::find(extensions.begin(), extensions.end(), lowercase(name.substr(dot + 1))) != extensions.end();
    }

    bool isSelected(const String& path) const {
        return std::find(selectedPaths.begin(), selectedPaths.end(), path) != selectedPaths.end();
    }

    /** What the select button returns, or nothing when the selection isn't valid */
    Strings getResultPaths() const {
        // The name field: a file in the directory that is shown. Without a name, the directory itself when folders are allowed.
        auto typed = [this]() -> Strings {
            if (currentPath.empty()) {
                return {};
            }
            const String name = nameField != nullptr ? lv_textarea_get_text(nameField) : "";
            if (!name.empty()) {
                return { child_path(currentPath, name) };
            }
            if (has(WINDOW_FILE_DIALOG_FOLDER)) {
                return { currentPath };
            }
            return {};
        };

        // A single file can be typed, which also shows a tapped file
        if (has(WINDOW_FILE_DIALOG_NEW_FILE) && !has(WINDOW_FILE_DIALOG_MULTIPLE)) {
            return typed();
        }
        if (!selectedPaths.empty()) {
            return selectedPaths;
        }
        if (has(WINDOW_FILE_DIALOG_NEW_FILE)) {
            return typed();
        }
        if (has(WINDOW_FILE_DIALOG_FOLDER) && !currentPath.empty()) {
            return { currentPath };
        }
        return {};
    }

    void showError(const char* message) {
        if (errorLabel == nullptr) {
            return;
        }
        if (message != nullptr) {
            lv_label_set_text(errorLabel, message);
        }
        lv_obj_set_hidden(errorLabel, message == nullptr);
    }

    void updateSelectButton(bool fitPathBar = true) {
        if (selectButton == nullptr) {
            return;
        }
        const bool hidden = getResultPaths().empty();
        if (hidden == lv_obj_is_hidden(selectButton)) {
            return;
        }
        lv_obj_set_hidden(selectButton, hidden);
        // The segments are fit to the room that the toolbar's other buttons leave
        if (fitPathBar && !currentPath.empty()) {
            lvgl_path_bar_set_path(pathBar, currentPath.c_str());
        }
    }

    void navigateTo(const String& path, bool selectFirst) {
        if (files == nullptr) {
            return;
        }
        if (lvgl_files_set_path(files, path.c_str(), path != currentPath, selectFirst) != ERROR_NONE) {
            LOG_E(TAG, "Failed to queue reading %s", path.c_str());
            showError(tr("Failed to open the folder"));
        }
    }

    void onLoaded(const String& path, error_t error) {
        if (files == nullptr) {
            return;
        }
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to read %s", path.c_str());
            // The first directory doesn't exist, e.g. the data directory without an SD card
            if (currentPath.empty() && path != "/") {
                navigateTo("/", !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
            } else {
                showError(tr("Failed to open the folder"));
            }
            return;
        }
        const bool changed = path != currentPath;
        currentPath = path;
        // A single file is picked in the directory that is shown. Multiple files are kept across directories.
        // The typed name stays, so a name can be typed before the directory is picked
        if (changed && !has(WINDOW_FILE_DIALOG_MULTIPLE)) {
            selectedPaths.clear();
        }
        showError(nullptr);
        // The path bar fits the room that the select button leaves, so the button is updated first
        updateSelectButton(false);
        lvgl_path_bar_set_path(pathBar, path.c_str());
        if (selectParentSegment) {
            selectParentSegment = false;
            lvgl_path_bar_focus_parent_segment(pathBar);
        }
    }

    void setTileChecked(const String& name, bool checked) {
        lv_obj_t* tile = lvgl_files_find_tile(files, name.c_str());
        if (tile != nullptr) {
            lv_obj_set_state(tile, LV_STATE_CHECKED, checked);
        }
    }

    void onTapFile(const String& path) {
        const auto name = last_segment(path);
        if (has(WINDOW_FILE_DIALOG_MULTIPLE)) {
            const auto found = std::find(selectedPaths.begin(), selectedPaths.end(), path);
            const bool select = found == selectedPaths.end();
            if (select) {
                selectedPaths.push_back(path);
            } else {
                selectedPaths.erase(found);
            }
            setTileChecked(name, select);
        } else {
            // One file at a time: tapping it again deselects it
            const bool deselect = isSelected(path);
            for (const auto& previous : selectedPaths) {
                setTileChecked(last_segment(previous), false);
            }
            selectedPaths.clear();
            if (!deselect) {
                selectedPaths.push_back(path);
            }
            setTileChecked(name, !deselect);
            if (nameField != nullptr) {
                lv_textarea_set_text(nameField, deselect ? "" : name.c_str());
            }
        }
        showError(nullptr);
        updateSelectButton();
    }

    void pick() {
        pickedPaths = getResultPaths();
        if (!pickedPaths.empty()) {
            // Checking them accesses files, which the LVGL task must not do
            dialog_content_post_work(handle);
        }
    }

    // region Callbacks

    // Runs on the file operations task
    static bool filterEntry(const char*, const dirent* entry, void* userData) {
        return lvgl_files_entry_is_directory(entry) || static_cast<FileDialog*>(userData)->isSelectableFile(entry->d_name);
    }

    static bool isEntryChecked(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
        auto* self = static_cast<FileDialog*>(userData);
        return !lvgl_files_entry_is_directory(entry) && self->isSelected(child_path(self->currentPath, entry->d_name));
    }

    static void onEntryClicked(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
        auto* self = static_cast<FileDialog*>(userData);
        if (self->files == nullptr) {
            return;
        }
        const auto path = child_path(self->currentPath, entry->d_name);
        if (lvgl_files_entry_is_directory(entry)) {
            // A touch doesn't show a selection. Without a pointer device there is no touch, so a selection is always wanted.
            lv_obj_t* tile = lvgl_files_find_tile(self->files, entry->d_name);
            const bool fromKeys = tile != nullptr && lv_obj_has_state(tile, LV_STATE_FOCUS_KEY);
            self->navigateTo(path, fromKeys || !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
        } else if (lvgl_files_entry_is_link(entry)) {
            LOG_W(TAG, "Opening links is not supported");
        } else {
            self->onTapFile(path);
        }
    }

    // The tiles are built after the dialog added its Escape handlers
    static void onEntryKey(lv_obj_t*, const dirent*, uint32_t, uint32_t key, void* userData) {
        auto* self = static_cast<FileDialog*>(userData);
        if (key == LV_KEY_ESC) {
            dialog_content_close_locked(self->handle, false);
        }
    }

    static void onDirectoryLoaded(lv_obj_t*, const char* path, error_t error, void* userData) {
        static_cast<FileDialog*>(userData)->onLoaded(path, error);
    }

    static void onPathSegmentPressed(lv_obj_t*, const char* path, bool fromKeys, void* userData) {
        auto* self = static_cast<FileDialog*>(userData);
        self->selectParentSegment = fromKeys;
        self->navigateTo(path, false);
    }

    // The grid can still hold a deleted segment, as the fallback of a selection that waits for the repopulate
    static void onPathBarRebuilt(lv_obj_t* bar, void* userData) {
        auto* self = static_cast<FileDialog*>(userData);
        if (self->files != nullptr) {
            lvgl_tile_grid_replace_empty_fallback(lvgl_files_get_tile_grid(self->files), lvgl_path_bar_get_parent_segment(bar));
        }
    }

    static void onClosePressed(lv_event_t* event) {
        dialog_content_close_locked(static_cast<FileDialog*>(lv_event_get_user_data(event))->handle, false);
    }

    static void onSelectPressed(lv_event_t* event) {
        static_cast<FileDialog*>(lv_event_get_user_data(event))->pick();
    }

    static void onNameChanged(lv_event_t* event) {
        auto* self = static_cast<FileDialog*>(lv_event_get_user_data(event));
        self->showError(nullptr);
        self->updateSelectButton();
    }

    static void onNameReady(lv_event_t* event) {
        static_cast<FileDialog*>(lv_event_get_user_data(event))->pick();
    }

    // endregion

public:

    FileDialog(const WindowFileDialogOptions& options, WindowFileDialogResultFn onResult, void* userData) :
        flags(options.flags),
        initialPath(options.path != nullptr ? options.path : ""),
        initialName(options.name != nullptr ? options.name : ""),
        onResult(onResult),
        userData(userData)
    {
        for (uint32_t i = 0; i < options.extension_count; i++) {
            const char* extension = options.extensions[i];
            extensions.push_back(lowercase(extension[0] == '.' ? extension + 1 : extension));
        }
    }

    void build(void* dialogHandle, lv_obj_t* card, lv_group_t* group) override {
        handle = dialogHandle;

        // The path bar takes the place of the title, and the select button is at its right
        lv_obj_t* toolbar = lvgl_toolbar_create(card, "");
        // The card doesn't clip its corners, as that takes much LVGL stack. A transparent toolbar keeps them round.
        lv_obj_set_style_bg_opa(toolbar, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onClosePressed, this);
        selectButton = lvgl_toolbar_add_text_button_action(toolbar, LVGL_ICON_SHARED_CHECK, onSelectPressed, this);
        lv_obj_set_style_text_font(selectButton, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_obj_set_hidden(selectButton, true);
        pathBar = lvgl_path_bar_create(toolbar, onPathSegmentPressed, onPathBarRebuilt, this);

        const LvglFilesConfig config = {
            .callbacks = {
                .classify = nullptr,
                .filter = filterEntry,
                .get_icon = nullptr,
                .is_checked = isEntryChecked,
                .on_clicked = onEntryClicked,
                .on_long_pressed = nullptr,
                .on_key = onEntryKey,
                .on_page_changed = nullptr,
                .on_loaded = onDirectoryLoaded,
                .user_data = this,
            },
            .swipe_navigation = true,
            .paging = &PAGING,
            .page = 0,
        };
        files = lvgl_files_create(card, &config);
        // The view, its grid and the grid's tile container are plain objects, which the theme draws as boxes of their own.
        // In a card they must show the card, so the entries join up with the toolbar.
        lv_obj_t* grid = lvgl_files_get_tile_grid(files);
        for (lv_obj_t* object : { files, grid, lv_obj_get_child(grid, 0) }) {
            lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(object, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_radius(object, 0, LV_STATE_DEFAULT);
        }

        errorLabel = lv_label_create(card);
        lv_obj_set_width(errorLabel, LV_PCT(100));
        lv_obj_set_style_text_align(errorLabel, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(errorLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_obj_set_hidden(errorLabel, true);

        if (has(WINDOW_FILE_DIALOG_NEW_FILE)) {
            nameField = lv_textarea_create(card);
            // Creating a textarea can add it to the shared keyboard group of the apps, which isn't the active group now
            lv_group_remove_obj(nameField);
            lv_group_add_obj(group, nameField);
            lv_textarea_set_one_line(nameField, true);
            lv_textarea_set_placeholder_text(nameField, tr("Enter filename"));
            lv_obj_set_width(nameField, LV_PCT(100));
            lv_textarea_set_text(nameField, initialName.c_str());
            lv_obj_add_event_cb(nameField, onNameChanged, LV_EVENT_VALUE_CHANGED, this);
            lv_obj_add_event_cb(nameField, onNameReady, LV_EVENT_READY, this);
        }

        // The view falls back to the root when the directory can't be read
        String path;
        if (!initialPath.empty()) {
            path = absolute_path(initialPath);
        } else {
            char root[FILE_MAX_PATH_STRING_LENGTH];
            path = paths_get_root_path(root, sizeof(root)) == ERROR_NONE ? root : "/";
        }
        lvgl_path_bar_set_path(pathBar, path.c_str());
        // Without a pointer device, keys need a selection to start from
        navigateTo(path, !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
    }

    void detach() override {
        selectButton = nullptr;
        pathBar = nullptr;
        files = nullptr;
        errorLabel = nullptr;
        nameField = nullptr;
    }

    void work() override {
        lvgl_lock();
        const auto paths = std::move(pickedPaths);
        pickedPaths.clear();
        lvgl_unlock();
        if (paths.empty()) {
            return;
        }

        const char* error = nullptr;
        for (const auto& path : paths) {
            const bool isFolder = directory_exists(path.c_str());
            const bool isFile = !isFolder && file_exists(path.c_str());
            // A path that doesn't exist yet is a new file
            if (!isFolder && !isFile && !has(WINDOW_FILE_DIALOG_NEW_FILE)) {
                error = tr("File not found");
            } else if (isFolder ? !has(WINDOW_FILE_DIALOG_FOLDER) : !isSelectableFile(path)) {
                error = tr("Can't select this");
            }
            if (error != nullptr) {
                break;
            }
        }

        lvgl_lock();
        if (error != nullptr) {
            showError(error);
        } else if (files != nullptr) {
            result = paths;
            dialog_content_close_locked(handle, true);
        }
        lvgl_unlock();
    }

    // A directory read that was pending when the widgets were deleted still calls filterEntry().
    // The file operations run in order, so it has completed when work that is queued after it has.
    void release() override {
        TaskEventGroup eventGroup {};
        task_event_group_construct(&eventGroup);
        FileOpsSubscription subscription {};
        error_t error;
        // A new event group has free bits, so only a lack of memory fails this. Not waiting would free what a read still uses.
        while ((error = file_ops_subscribe(&subscription, &eventGroup)) == ERROR_OUT_OF_MEMORY) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        check(error == ERROR_NONE);
        // Other failures than a lack of memory only happen when nothing is queued, so no read is pending then
        while ((error = file_ops_queue([](void*) {}, nullptr, &subscription)) == ERROR_OUT_OF_MEMORY) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (error == ERROR_NONE) {
            file_ops_await(&subscription, portMAX_DELAY);
        }
        file_ops_unsubscribe(&subscription);
        task_event_group_destruct(&eventGroup);
    }

    void deliver(bool confirmed) override {
        if (onResult == nullptr) {
            return;
        }
        std::vector<const char*> paths;
        if (confirmed) {
            for (const auto& path : result) {
                paths.push_back(path.c_str());
            }
        }
        onResult(paths.empty() ? nullptr : paths.data(), static_cast<uint32_t>(paths.size()), userData);
    }
};

} // namespace

extern "C" error_t window_manager_file_dialog(
    AppInstanceId app_instance_id,
    const struct WindowFileDialogOptions* options,
    WindowFileDialogResultFn on_result,
    void* user_data
) {
    const WindowFileDialogOptions defaults {};
    const auto& used = options != nullptr ? *options : defaults;
    if (app_instance_id == 0 || (used.extension_count > 0 && used.extensions == nullptr)) {
        return ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < used.extension_count; i++) {
        if (used.extensions[i] == nullptr) {
            return ERROR_INVALID_ARGUMENT;
        }
    }
    DialogContentPtr content(tt::newObject<FileDialog>(used, on_result, user_data));
    if (content == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    return dialog_start_content(app_instance_id, std::move(content));
}
