#define L10N_APP "fileselection"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/TileGrid.h>
#include <Tactility/file/File.h>

#include <app/event.h>

#include <tactility/log.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/path_bar.h>
#include <lvgl/widgets/toolbar.h>

#include <cstring>

// Included last, as it makes std:: resolve to tt::std
#include <Tactility/app/fileselection/View.h>

namespace tt::app::fileselection {

constexpr auto* TAG = "FileSelection";

View::View(uint32_t appInstanceId, const ::std::shared_ptr<State>& state, ::std::function<void(const ::std::vector<::std::string>& paths)> onFileSelected) :
    appInstanceId(appInstanceId),
    state(state),
    on_file_selected(::std::move(onFileSelected))
{}

// region Directory

void View::navigateTo(const ::std::string& path, bool selectFirst) {
    if (files == nullptr) {
        return;
    }
    // Another directory starts at its first page. The same one keeps its page, e.g. when the widgets were rebuilt.
    const bool firstPage = !state->isLoaded() || path != state->getCurrentPath();
    if (lvgl_files_set_path(files, path.c_str(), firstPage, selectFirst) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue reading %s", path.c_str());
    }
}

void View::onLoaded(lv_obj_t*, const char* path, error_t error, void* userData) {
    static_cast<View*>(userData)->onDirectoryLoaded(path, error);
}

void View::onDirectoryLoaded(const ::std::string& path, error_t error) {
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to read %s", path.c_str());
        // The first directory doesn't exist, e.g. the data directory without an SD card
        if (!state->isLoaded() && path != "/") {
            navigateTo("/", !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
        }
        return;
    }

    const auto& options = state->getOptions();
    const bool changed = !state->isLoaded() || path != state->getCurrentPath();
    state->setCurrentPath(path);
    if (changed) {
        // A single file is picked in the directory that is shown. Multiple files are kept across directories.
        // The typed name stays, so a name can be typed before the directory is picked
        if (!options.multiple) {
            state->setSelectedPaths({});
        }
    }

    // The path bar fits the room that the select button leaves, so the button is updated first
    updateSelectButton(false);
    // Also replaces the fallback of the selection, the nearest segment, for when the directory has no entries
    lvgl_path_bar_set_path(path_bar, path.c_str());
    // The pressed segment is gone, so the selection moves to the segment below the new directory
    if (selectParentSegment) {
        selectParentSegment = false;
        lvgl_path_bar_focus_parent_segment(path_bar);
    }
}

// Runs on the file operations task
bool View::filterEntry(const char*, const dirent* entry, void* userData) {
    auto* view = static_cast<View*>(userData);
    return lvgl_files_entry_is_directory(entry) || view->state->isSelectableFile(entry->d_name);
}

bool View::isEntryChecked(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
    auto* view = static_cast<View*>(userData);
    if (lvgl_files_entry_is_directory(entry)) {
        return false;
    }
    return view->state->isSelectedPath(file::getChildPath(view->state->getCurrentPath(), entry->d_name));
}

void View::onEntryClicked(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
    auto* view = static_cast<View*>(userData);
    const ::std::string child_path = file::getChildPath(view->state->getCurrentPath(), entry->d_name);
    if (lvgl_files_entry_is_directory(entry)) {
        // A touch doesn't show a selection. Without a pointer device there is no touch, so a selection is always wanted.
        lv_obj_t* tile = lvgl_files_find_tile(view->files, entry->d_name);
        const bool from_keys = tile != nullptr && lv_obj_has_state(tile, LV_STATE_FOCUS_KEY);
        view->navigateTo(child_path, from_keys || !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
    } else if (lvgl_files_entry_is_link(entry)) {
        LOG_W(TAG, "Opening links is not supported");
    } else {
        view->onTapFile(child_path);
    }
}

void View::onPathSegmentPressed(lv_obj_t*, const char* path, bool fromKeys, void* userData) {
    auto* view = static_cast<View*>(userData);
    view->selectParentSegment = fromKeys;
    view->navigateTo(path, false);
}

// The grid can still hold a deleted segment, as the fallback of a selection that waits for the repopulate
void View::onPathBarRebuilt(lv_obj_t* bar, void* userData) {
    auto* view = static_cast<View*>(userData);
    if (view->files != nullptr) {
        lvgl_tile_grid_replace_empty_fallback(lvgl_files_get_tile_grid(view->files), lvgl_path_bar_get_parent_segment(bar));
    }
}

// endregion

// region Selection

void View::setTileChecked(const ::std::string& name, bool checked) {
    lv_obj_t* tile = files != nullptr ? lvgl_files_find_tile(files, name.c_str()) : nullptr;
    if (tile != nullptr) {
        lv_obj_set_state(tile, LV_STATE_CHECKED, checked);
    }
}

void View::onTapFile(const ::std::string& filePath) {
    LOG_D(TAG, "Clicked %s", filePath.c_str());
    const auto& options = state->getOptions();
    const auto name = file::getLastPathSegment(filePath);
    if (options.multiple) {
        setTileChecked(name, state->toggleSelectedPath(filePath));
    } else {
        // One file at a time: tapping it again deselects it
        const auto previous = state->getSelectedPaths();
        const bool deselect = !previous.empty() && previous.front() == filePath;
        for (const auto& path : previous) {
            setTileChecked(file::getLastPathSegment(path), false);
        }
        state->setSelectedPaths(deselect ? ::std::vector<::std::string> {} : ::std::vector<::std::string> { filePath });
        setTileChecked(name, !deselect);
        if (path_textarea != nullptr) {
            // Updates nameText through onNameTextChanged
            lv_textarea_set_text(path_textarea, deselect ? "" : name.c_str());
        }
    }
    updateSelectButton();
}

::std::vector<::std::string> View::getResultPaths() const {
    const auto& options = state->getOptions();
    // The name field: a file in the directory that is shown. Without a name, the directory itself when folders are allowed.
    auto getTypedPath = [&]() -> ::std::vector<::std::string> {
        if (!state->isLoaded()) {
            return {};
        }
        const ::std::string name = nameText.c_str();
        if (name.empty()) {
            if (options.allowFolder) {
                return { state->getCurrentPath() };
            }
            return {};
        }
        return { file::getChildPath(state->getCurrentPath(), name) };
    };

    // A single file can be typed, which also shows a tapped file
    if (options.allowNewFile && !options.multiple) {
        return getTypedPath();
    }
    auto paths = state->getSelectedPaths();
    if (!paths.empty()) {
        return paths;
    }
    if (options.allowNewFile) {
        return getTypedPath();
    }
    // Whether it is a folder is checked by the app task, as that needs file operations
    if (options.allowFolder && state->isLoaded()) {
        return { state->getCurrentPath() };
    }
    return {};
}

void View::updateSelectButton(bool fitPathBar) {
    if (select_button == nullptr) {
        return;
    }
    const bool hidden = getResultPaths().empty();
    if (hidden == lv_obj_is_hidden(select_button)) {
        return;
    }
    lv_obj_set_hidden(select_button, hidden);
    // The segments are fit to the room that the toolbar's other buttons leave
    if (fitPathBar && state->isLoaded()) {
        lvgl_path_bar_set_path(path_bar, state->getCurrentPath().c_str());
    }
}

void View::onSelectButtonPressed(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto paths = view->getResultPaths();
    if (paths.empty()) {
        LOG_W(TAG, "Select pressed, but no valid path found");
        return;
    }
    view->onFileSelected(paths);
}

void View::onNameTextChanged(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->nameText = lv_textarea_get_text(view->path_textarea);
    view->updateSelectButton();
}

// endregion

void View::onBackPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    app_event_emit_close(view->appInstanceId);
}

void View::init(lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    // The path bar takes the place of the title, and the select button is at its right
    toolbar = lvgl_toolbar_create(parent, "");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, &onBackPressedCallback, this);
    select_button = lvgl_toolbar_add_text_button_action(toolbar, LVGL_ICON_SHARED_CHECK, onSelectButtonPressed, this);
    lv_obj_set_style_text_font(select_button, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_obj_set_hidden(select_button, true);
    path_bar = lvgl_path_bar_create(toolbar, onPathSegmentPressed, onPathBarRebuilt, this);
    // Before the segments are built, as they fit the room that the button leaves
    updateSelectButton(false);
    lvgl_path_bar_set_path(path_bar, state->getCurrentPath().c_str());

    const LvglFilesConfig config = {
        .callbacks = {
            .classify = nullptr,
            .filter = filterEntry,
            .get_icon = nullptr,
            .is_checked = isEntryChecked,
            .on_clicked = onEntryClicked,
            .on_long_pressed = nullptr,
            .on_key = nullptr,
            .on_page_changed = nullptr,
            .on_loaded = onLoaded,
            .user_data = this,
        },
        .swipe_navigation = true,
        .paging = getSystemBarsPaging(),
        .page = page,
    };
    files = lvgl_files_create(parent, &config);

    if (state->getOptions().allowNewFile) {
        auto* bottom_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(bottom_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(bottom_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(bottom_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(bottom_wrapper, 0, LV_STATE_DEFAULT);

        path_textarea = lv_textarea_create(bottom_wrapper);
        lv_textarea_set_one_line(path_textarea, true);
        lv_textarea_set_placeholder_text(path_textarea, tr("Enter filename"));
        lv_obj_set_flex_grow(path_textarea, 1);
        // Rebuilt from the cached text
        lv_textarea_set_text(path_textarea, nameText.c_str());
        lv_obj_add_event_cb(path_textarea, onNameTextChanged, LV_EVENT_VALUE_CHANGED, this);
    }

    // The new widget reads the directory again. Without a pointer device, keys need a selection to start from.
    navigateTo(state->getCurrentPath(), !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
}

void View::destroyWidgets() {
    page = lvgl_tile_grid_get_page(lvgl_files_get_tile_grid(files));
    toolbar = nullptr;
    path_bar = nullptr;
    select_button = nullptr;
    files = nullptr;
    path_textarea = nullptr;
    selectParentSegment = false;
}

}
