#include <Tactility/app/notes/Notes.h>
#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/file/File.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/log.h>

namespace tt::app::notes {

constexpr auto* TAG = "Notes";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* uiCurrentFileName = nullptr;
    lv_obj_t* uiDropDownMenu = nullptr;
    lv_obj_t* uiNoteText = nullptr;

    std::string filePath;
    std::string saveBuffer;

    uint32_t loadFileLaunchId = 0;
    uint32_t saveFileLaunchId = 0;
};


void resetFileContent(Context* ctx) {
    lv_textarea_set_text(ctx->uiNoteText, "");
    ctx->filePath = "";
    ctx->saveBuffer = "";
    lv_label_set_text(ctx->uiCurrentFileName, "Untitled");
}

void openFile(Context* ctx, const std::string& path) {
    // We might be reading from the SD card, which could share a SPI bus with other devices (display)
    file::FileMutexGuard guard(path);
    auto data = file::readString(path);
    if (data != nullptr) {
        lvgl_lock();
        lv_textarea_set_text(ctx->uiNoteText, reinterpret_cast<const char*>(data.get()));
        lv_label_set_text(ctx->uiCurrentFileName, path.c_str());
        lvgl_unlock();
        ctx->filePath = path;
        LOG_I(TAG, "Loaded from %s", path.c_str());
    }
}

bool saveFile(Context* ctx, const std::string& path) {
    // We might be writing to SD card, which could share a SPI bus with other devices (display)
    bool result = false;
    {
        file::FileMutexGuard guard(path);
        if (file::writeString(path, ctx->saveBuffer.c_str())) {
            LOG_I(TAG, "Saved to %s", path.c_str());
            ctx->filePath = path;
            result = true;
        }
    }
    return result;
}

void appNotesEventCb(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target_obj(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (obj == ctx->uiDropDownMenu) {
            switch (lv_dropdown_get_selected(obj)) {
                case 0: // New
                    resetFileContent(ctx);
                    break;
                case 1: // Save
                    if (!ctx->filePath.empty()) {
                        lvgl_lock();
                        ctx->saveBuffer = lv_textarea_get_text(ctx->uiNoteText);
                        lvgl_unlock();
                        saveFile(ctx, ctx->filePath);
                    }
                    break;
                case 2: // Save as...
                    lvgl_lock();
                    ctx->saveBuffer = lv_textarea_get_text(ctx->uiNoteText);
                    lvgl_unlock();
                    ctx->saveFileLaunchId = fileselection::startForExistingOrNewFile(ctx->appInstanceId);
                    LOG_I(TAG, "launched with id %u", ctx->saveFileLaunchId);
                    break;
                case 3: // Load
                    ctx->loadFileLaunchId = fileselection::startForExistingFile(ctx->appInstanceId);
                    LOG_I(TAG, "launched with id %u", ctx->loadFileLaunchId);
                    break;
            }
        } else {
            auto* cont = lv_event_get_current_target_obj(e);
            if (obj == cont) return;
            if (lv_obj_get_child(cont, 1)) {
                ctx->saveFileLaunchId = fileselection::startForExistingOrNewFile(ctx->appInstanceId);
                LOG_I(TAG, "launched with id %u", ctx->saveFileLaunchId);
            } else { //Reset
                resetFileContent(ctx);
            }
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Notes");
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    ctx->uiDropDownMenu = lv_dropdown_create(toolbar);
    lv_dropdown_set_options(ctx->uiDropDownMenu, LV_SYMBOL_FILE " New File\n" LV_SYMBOL_SAVE " Save\n" LV_SYMBOL_SAVE " Save As...\n" LV_SYMBOL_DIRECTORY " Open File");
    lv_dropdown_set_text(ctx->uiDropDownMenu, "Menu");
    lv_dropdown_set_symbol(ctx->uiDropDownMenu, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected_highlight(ctx->uiDropDownMenu, false);
    lv_obj_align(ctx->uiDropDownMenu, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->uiDropDownMenu, appNotesEventCb, LV_EVENT_VALUE_CHANGED, ctx);

    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_height(wrapper, LV_PCT(100));
    lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrapper, 0, LV_PART_MAIN);
    lv_obj_remove_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);

    ctx->uiNoteText = lv_textarea_create(wrapper);
    lv_obj_set_width(ctx->uiNoteText, LV_PCT(100));
    lv_obj_set_height(ctx->uiNoteText, LV_PCT(86));
    lv_textarea_set_password_mode(ctx->uiNoteText, false);
    if (lv_display_get_color_format(lv_obj_get_display(parent)) != LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_bg_color(ctx->uiNoteText, lv_color_hex(0x262626), LV_PART_MAIN);
    }
    lv_textarea_set_placeholder_text(ctx->uiNoteText, "Notes...");

    lv_obj_t* footer = lv_obj_create(wrapper);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (lv_display_get_color_format(lv_obj_get_display(parent)) == LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_bg_color(footer, lv_color_hex(0xEEEEEE), LV_PART_MAIN);
        lv_obj_set_style_border_width(footer, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(footer, lv_theme_get_color_secondary(footer), LV_PART_MAIN);
        lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x262626), LV_PART_MAIN);
        lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
    }
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, LV_PCT(14));
    lv_obj_set_style_pad_all(footer, 0, LV_PART_MAIN);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    ctx->uiCurrentFileName = lv_label_create(footer);
    lv_label_set_long_mode(ctx->uiCurrentFileName, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(ctx->uiCurrentFileName, LV_SIZE_CONTENT);
    lv_obj_set_height(ctx->uiCurrentFileName, LV_SIZE_CONTENT);
    lv_label_set_text(ctx->uiCurrentFileName, "Untitled");
    lv_obj_align(ctx->uiCurrentFileName, LV_ALIGN_CENTER, 0, 0);

    if (!ctx->filePath.empty()) {
        openFile(ctx, ctx->filePath);
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    if (argc > 0 && argv[0][0] != '\0') {
        ctx.filePath = argv[0];
    }

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            case APP_EVENT_RESULT:
                LOG_I(TAG, "Result for launch id %u", event.result.launch_id);
                if (event.result.launch_id == ctx.loadFileLaunchId) {
                    ctx.loadFileLaunchId = 0;
                    if (event.result.result == 0 /* Ok */) {
                        auto path = fileselection::getLastPath();
                        if (!path.empty()) {
                            openFile(&ctx, path);
                        }
                    }
                } else if (event.result.launch_id == ctx.saveFileLaunchId) {
                    ctx.saveFileLaunchId = 0;
                    if (event.result.result == 0 /* Ok */) {
                        auto path = fileselection::getLastPath();
                        // Must re-open file, because the UI was cleared after opening the dialog.
                        if (!path.empty() && saveFile(&ctx, path)) {
                            openFile(&ctx, path);
                        }
                    }
                }
                app_manager_stop(event.result.launch_id);
                break;
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

void start(const std::string& filePath) {
    const char* argv[] = { filePath.c_str() };
    uint32_t instanceId = 0;
    app_manager_start_with_parameters(manifest.id, 1, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "Notes",
    .name = "Notes",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::notes
