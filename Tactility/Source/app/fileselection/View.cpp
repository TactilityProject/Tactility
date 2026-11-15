#include <Tactility/StringUtils.h>
#include <Tactility/file/File.h>

#include <app/event.h>

#include <tactility/check.h>
#include <tactility/file_ops.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <cstring>
#include <unistd.h>

// Included last, as it makes std:: resolve to tt::std
#include <Tactility/app/fileselection/View.h>

#ifdef ESP_PLATFORM
#endif

namespace tt::app::fileselection {

constexpr auto* TAG = "FileSelection";

// region Callbacks

static void onDirEntryPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto index = lv_obj_get_index(button);
    view->onDirEntryPressed(index, button);
}

static void onNavigateUpPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNavigateUpPressed();
}

// endregion

namespace {

struct NavigateJob {
    ::std::shared_ptr<State> state;
    ::std::string path;
};

void navigateWork(void* context) {
    auto* job = static_cast<NavigateJob*>(context);
    if (job->state->setEntriesForPath(job->path)) {
        job->state->pushCompletedNavigation();
    }
    delete job;
}

} // namespace

View::View(uint32_t appInstanceId, const ::std::shared_ptr<State>& state, TaskEventGroup* eventGroup, ::std::function<void(const ::std::vector<::std::string>& paths)> onFileSelected) :
    appInstanceId(appInstanceId),
    state(state),
    on_file_selected(::std::move(onFileSelected))
{
    check(file_ops_subscribe(&fileOpsSub, eventGroup) == ERROR_NONE);
}

View::~View() {
    file_ops_unsubscribe(&fileOpsSub);
}

void View::navigateTo(const ::std::string& path) {
    if (navigationPending.exchange(true)) {
        return;
    }
    auto* job = new (::std::nothrow) NavigateJob { state, path };
    if (job == nullptr || file_ops_queue(navigateWork, job, &fileOpsSub) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue reading %s", path.c_str());
        delete job;
        navigationPending = false;
    }
}

void View::onFileOpsCompleted() {
    while (file_ops_poll(&fileOpsSub) == ERROR_NONE) {
        navigationPending = false;
        if (state->takeCompletedNavigation()) {
            lvgl_lock();
            lv_textarea_set_text(path_textarea, state->getCurrentPathWithTrailingSlash().c_str());
            lvgl_unlock();
            update();
        }
    }
}

void View::onBackPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    app_event_emit_close(view->appInstanceId);
}

void View::onTapFile(const ::std::string& path, const ::std::string& filename, lv_obj_t* button) {
    ::std::string file_path = file::getChildPath(path, filename);
    LOG_D(TAG, "Clicked %s", file_path.c_str());
    if (state->getOptions().multiple) {
        lv_obj_set_state(button, LV_STATE_CHECKED, state->toggleSelectedPath(file_path));
    } else {
        lv_textarea_set_text(path_textarea, file_path.c_str());
    }
    updateSelectButton();
}

void View::onDirEntryPressed(uint32_t index, lv_obj_t* button) {
    dirent dir_entry;
    if (state->getDirent(index, dir_entry)) {
        LOG_D(TAG, "Pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);
        state->setSelectedChildEntry(dir_entry.d_name);
        using namespace tt::file;
        switch (dir_entry.d_type) {
            case TT_DT_DIR:
            case TT_DT_CHR:
                navigateTo(file::getChildPath(state->getCurrentPath(), dir_entry.d_name));
                break;
            case TT_DT_LNK:
                LOG_W(TAG, "Opening links is not supported");
                break;
            case TT_DT_REG:
                onTapFile(state->getCurrentPath(), dir_entry.d_name, button);
                break;
            default:
                // Assume it's a file
                // TODO: Find a better way to identify a file
                onTapFile(state->getCurrentPath(), dir_entry.d_name, button);
                break;
        }
    }
}

::std::vector<::std::string> View::getResultPaths() const {
    auto paths = state->getSelectedPaths();
    if (!paths.empty()) {
        return paths;
    }
    const char* text = lv_textarea_get_text(path_textarea);
    if (text == nullptr || strlen(text) == 0) {
        return paths;
    }
    // Whether it is a file or a folder is checked by the app task, as that needs file operations
    ::std::string path = text;
    while (path.length() > 1 && path.back() == '/') {
        path.pop_back();
    }
    paths.push_back(::std::move(path));
    return paths;
}

void View::updateSelectButton() {
    auto paths = getResultPaths();
    lv_obj_set_hidden(select_button, paths.empty());
    if (state->getOptions().multiple && paths.size() > 1) {
        lv_label_set_text_fmt(select_label, "Select (%u)", static_cast<unsigned>(paths.size()));
    } else {
        lv_label_set_text(select_label, "Select");
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

void View::onPathTextChanged(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->updateSelectButton();
}

void View::createDirEntryWidget(lv_obj_t* list, dirent& dir_entry) {
    check(list);
    const char* symbol;
    if (dir_entry.d_type == file::TT_DT_DIR || dir_entry.d_type == file::TT_DT_CHR) {
        symbol = LV_SYMBOL_DIRECTORY;
    } else {
        symbol = LV_SYMBOL_FILE;
    }
    lv_obj_t* button = lvgl_list_add_button(list, symbol, dir_entry.d_name);
    lv_obj_add_event_cb(button, &onDirEntryPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    if (state->getOptions().multiple && dir_entry.d_type != file::TT_DT_DIR && dir_entry.d_type != file::TT_DT_CHR && state->isSelectedPath(file::getChildPath(state->getCurrentPath(), dir_entry.d_name))) {
        lv_obj_add_state(button, LV_STATE_CHECKED);
    }
}

void View::onNavigateUpPressed() {
    if (state->getCurrentPath() != "/") {
        LOG_D(TAG, "Navigating upwards");
        ::std::string new_absolute_path;
        if (string::getPathParent(state->getCurrentPath(), new_absolute_path)) {
            navigateTo(new_absolute_path);
        }
    }
}

void View::update() {
    if (!lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "lvgl");
        return;
    }

    lv_obj_clean(dir_entry_list);

    state->withEntries([this](const ::std::vector<dirent>& entries) {
        for (auto entry : entries) {
            LOG_D(TAG, "Entry: %s %d", entry.d_name, (int)entry.d_type);
            createDirEntryWidget(dir_entry_list, entry);
        }
    });

    if (state->getCurrentPath() == "/") {
        lv_obj_set_hidden(navigate_up_button, true);
    } else {
        lv_obj_set_hidden(navigate_up_button, false);
    }

    lvgl_unlock();
}

void View::init(lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Select File");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, &onBackPressedCallback, this);
    navigate_up_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_UP, &onNavigateUpPressedCallback, this);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);

    dir_entry_list = lvgl_list_create(wrapper);
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
    select_label = lv_label_create(select_button);
    lv_label_set_text(select_label, "Select");
    lv_obj_add_event_cb(select_button, onSelectButtonPressed, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_set_hidden(select_button, true);

    update();
    updateSelectButton();
}

}
