#define L10N_APP "notes"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/notes/Notes.h>


#include <Tactility/app/dialog/Dialog.h>
#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/file/File.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/check.h>
#include <tactility/file_ops.h>
#include <tactility/log.h>

#include <new>
#include <vector>

// Included last, as it makes std:: resolve to tt::std
#include <tactility/std/vector.hpp>

namespace tt::app::notes {

constexpr auto* TAG = "Notes";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;

    // Null while the window has no widgets. Accessed with the LVGL lock held.
    lv_obj_t* uiToolbar = nullptr;
    lv_obj_t* uiNoteText = nullptr;

    ::std::string filePath;
    ::std::string saveBuffer;

    /** Signalled when the menu dialog closed, as its result arrives on the dialog's task */
    uint32_t menuBit = 0;
    int32_t menuResult = WINDOW_DIALOG_DISMISSED;
    /** The actions of the items of the menu that is shown */
    tt::std::vector<int> menuActions;

    uint32_t loadFileLaunchId = 0;
    uint32_t saveFileLaunchId = 0;
    AppStream loadResultStream {};
    uint8_t loadResultBuffer[256] {};
    AppStream saveResultStream {};
    uint8_t saveResultBuffer[256] {};
};


/** The toolbar shows the name of the file, without its directory */
void updateTitle(Context* ctx) {
    const auto name = ctx->filePath.empty() ? ::std::string(tr("Untitled")) : file::getLastPathSegment(ctx->filePath);
    if (ctx->uiToolbar != nullptr) {
        lvgl_toolbar_set_title(ctx->uiToolbar, name.c_str());
    }
}

void resetFileContent(Context* ctx) {
    lv_textarea_set_text(ctx->uiNoteText, "");
    ctx->filePath = "";
    ctx->saveBuffer = "";
    updateTitle(ctx);
}

void openFile(Context* ctx, const ::std::string& path) {
    auto data = file::readString(path);
    if (data != nullptr) {
        lvgl_lock();
        // Without widgets, the file is read again when they are created
        ctx->filePath = path;
        if (ctx->uiNoteText != nullptr) {
            lv_textarea_set_text(ctx->uiNoteText, reinterpret_cast<const char*>(data.get()));
            updateTitle(ctx);
        }
        lvgl_unlock();
        LOG_I(TAG, "Loaded from %s", path.c_str());
    }
}

bool saveFile(Context* ctx, const ::std::string& path) {
    bool result = false;
    if (file::writeString(path, ctx->saveBuffer.c_str())) {
        LOG_I(TAG, "Saved to %s", path.c_str());
        ctx->filePath = path;
        result = true;
    }
    return result;
}

struct SaveJob {
    ::std::string path;
    ::std::string content;
};

/** File operation: it runs on the file operations task, which frees the job */
void saveInBackground(void* context) {
    auto* job = static_cast<SaveJob*>(context);
    if (file::writeString(job->path, job->content.c_str())) {
        LOG_I(TAG, "Saved to %s", job->path.c_str());
    } else {
        LOG_E(TAG, "Failed to save to %s", job->path.c_str());
    }
    delete job;
}

enum MenuAction { MENU_NEW, MENU_SAVE, MENU_SAVE_AS, MENU_OPEN };

void newFile(Context* ctx) {
    lvgl_lock();
    if (ctx->uiNoteText != nullptr) {
        resetFileContent(ctx);
    }
    lvgl_unlock();
}

void save(Context* ctx) {
    // The file operations task writes it
    lvgl_lock();
    if (ctx->uiNoteText == nullptr) {
        lvgl_unlock();
        return;
    }
    auto* job = new (::std::nothrow) SaveJob { ctx->filePath, lv_textarea_get_text(ctx->uiNoteText) };
    lvgl_unlock();
    if (job == nullptr || file_ops_queue(saveInBackground, job, nullptr) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue saving %s", ctx->filePath.c_str());
        delete job;
    }
}

void saveAs(Context* ctx) {
    lvgl_lock();
    if (ctx->uiNoteText == nullptr) {
        lvgl_unlock();
        return;
    }
    ctx->saveBuffer = lv_textarea_get_text(ctx->uiNoteText);
    lvgl_unlock();
    ctx->saveFileLaunchId = fileselection::startForExistingOrNewFile(ctx->appInstanceId, ctx->saveResultStream, ctx->saveResultBuffer, sizeof(ctx->saveResultBuffer), ctx->eventGroup);
    LOG_I(TAG, "launched with id %u", ctx->saveFileLaunchId);
}

void open(Context* ctx) {
    ctx->loadFileLaunchId = fileselection::startForExistingFile(ctx->appInstanceId, ctx->loadResultStream, ctx->loadResultBuffer, sizeof(ctx->loadResultBuffer), ctx->eventGroup);
    LOG_I(TAG, "launched with id %u", ctx->loadFileLaunchId);
}

/** Runs on the dialog's task */
void onMenuClosed(int32_t item, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->menuResult = item;
    task_event_group_signal(ctx->eventGroup, ctx->menuBit);
}

/** Runs on the app task */
void onMenuResult(Context* ctx) {
    const int32_t result = ctx->menuResult;
    if (result < 0 || static_cast<size_t>(result) >= ctx->menuActions.size()) {
        return;
    }
    switch (ctx->menuActions[result]) {
        case MENU_NEW:
            newFile(ctx);
            break;
        case MENU_SAVE:
            save(ctx);
            break;
        case MENU_SAVE_AS:
            saveAs(ctx);
            break;
        case MENU_OPEN:
            open(ctx);
            break;
        default:
            break;
    }
}

void onOverflowPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ::std::vector<::std::string> labels;
    ::std::vector<const char*> icons;
    ctx->menuActions.clear();
    auto add = [&](MenuAction action, const char* label, const char* icon) {
        ctx->menuActions.push_back(action);
        labels.emplace_back(label);
        icons.push_back(icon);
    };
    add(MENU_NEW, tr("New"), LVGL_ICON_SHARED_NOTE_ADD);
    // Saving needs a file, which "save as" picks
    if (!ctx->filePath.empty()) {
        add(MENU_SAVE, tr("Save"), LVGL_ICON_SHARED_SAVE);
    }
    add(MENU_SAVE_AS, tr("Save as"), LVGL_ICON_SHARED_SAVE_AS);
    add(MENU_OPEN, tr("Open"), LVGL_ICON_SHARED_FOLDER_OPEN);
    if (dialog::selection(ctx->appInstanceId, tr("Notes"), labels, icons, onMenuClosed, ctx) != ERROR_NONE) {
        LOG_E(TAG, "Failed to show the menu");
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_scrollable(parent, false);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    ctx->uiToolbar = lvgl_toolbar_create(parent, "");
    updateTitle(ctx);
    lv_obj_align(ctx->uiToolbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* overflow_button = lvgl_toolbar_add_text_button_action(ctx->uiToolbar, LVGL_ICON_SHARED_MORE_VERT, onOverflowPressed, ctx);
    lv_obj_set_style_text_font(overflow_button, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);

    ctx->uiNoteText = lv_textarea_create(parent);
    lv_obj_set_width(ctx->uiNoteText, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->uiNoteText, 1);
    lv_textarea_set_password_mode(ctx->uiNoteText, false);
    lv_textarea_set_placeholder_text(ctx->uiNoteText, tr("Notes..."));

    if (!ctx->filePath.empty()) {
        openFile(ctx, ctx->filePath);
    }
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->uiToolbar = nullptr;
    ctx->uiNoteText = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    if (argc > 0 && argv[0][0] != '\0') {
        ctx.filePath = argv[0];
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.menuBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.menuBit) {
            onMenuResult(&ctx);
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    LOG_I(TAG, "Result for launch id %u = %u", event.result.launch_id, event.result.result);
                    if (event.result.launch_id == ctx.loadFileLaunchId) {
                        ctx.loadFileLaunchId = 0;
                        if (event.result.result == 0 /* Ok */) {
                            char destination[sizeof(ctx.loadResultBuffer)];
                            size_t length = app_stream_read(&ctx.loadResultStream, destination, sizeof(destination));
                            app_stream_unsubscribe(&ctx.loadResultStream);
                            auto path = ::std::string(destination, length);
                            LOG_I(TAG, "Path: '%s'", path.c_str());
                            if (!path.empty()) {
                                openFile(&ctx, path);
                            }
                        } else {
                            app_stream_unsubscribe(&ctx.loadResultStream);
                        }
                    } else if (event.result.launch_id == ctx.saveFileLaunchId) {
                        ctx.saveFileLaunchId = 0;
                        if (event.result.result == 0 /* Ok */) {
                            char destination[sizeof(ctx.saveResultBuffer)];
                            size_t length = app_stream_read(&ctx.saveResultStream, destination, sizeof(destination));
                            app_stream_unsubscribe(&ctx.saveResultStream);
                            auto path = ::std::string(destination, length);
                            // Must re-open file, because the UI was cleared after opening the dialog.
                            LOG_I(TAG, "Path: '%s'", path.c_str());
                            if (!path.empty() && saveFile(&ctx, path)) {
                                openFile(&ctx, path);
                            }
                        } else {
                            app_stream_unsubscribe(&ctx.saveResultStream);
                        }
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_release_bit(&event_group, ctx.menuBit);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start(const ::std::string& filePath) {
    const char* argv[] = { filePath.c_str() };
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, 1, argv);
    app_start_with_context(&context, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.notes",
    .name = "Notes",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::notes
