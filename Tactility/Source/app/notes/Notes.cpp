#include <Tactility/app/notes/Notes.h>

#include "Tactility/app/alertdialog/AlertDialog.h"

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
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/insets.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/check.h>
#include <tactility/log.h>

namespace tt::app::notes {

constexpr auto* TAG = "Notes";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;

    lv_obj_t* uiCurrentFileName = nullptr;
    lv_obj_t* uiToolbar = nullptr;
    lv_obj_t* uiOverflowButton = nullptr;
    lv_obj_t* uiOverlay = nullptr;
    lv_obj_t* uiNoteText = nullptr;

    std::string filePath;
    std::string saveBuffer;

    uint32_t loadFileLaunchId = 0;
    uint32_t saveFileLaunchId = 0;
    AppStream loadResultStream {};
    uint8_t loadResultBuffer[256] {};
    AppStream saveResultStream {};
    uint8_t saveResultBuffer[256] {};
};


void resetFileContent(Context* ctx) {
    lv_textarea_set_text(ctx->uiNoteText, "");
    ctx->filePath = "";
    ctx->saveBuffer = "";
    lv_label_set_text(ctx->uiCurrentFileName, "Untitled");
}

void openFile(Context* ctx, const std::string& path) {
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
    bool result = false;
    if (file::writeString(path, ctx->saveBuffer.c_str())) {
        LOG_I(TAG, "Saved to %s", path.c_str());
        ctx->filePath = path;
        result = true;
    }
    return result;
}

void hideOverlay(Context* ctx) {
    if (lv_obj_is_hidden(ctx->uiOverlay)) {
        return;
    }
    lv_obj_set_hidden(ctx->uiOverlay, true);

    // Keys continue on the button that opened the overlay
    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && lv_group_get_focused(group) == ctx->uiOverlay) {
        lv_group_focus_obj(ctx->uiToolbar);
        lv_gridnav_set_focused(ctx->uiToolbar, ctx->uiOverflowButton, LV_ANIM_OFF);
    }
}

void onNewPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    hideOverlay(ctx);
    resetFileContent(ctx);
}

void onSavePressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    hideOverlay(ctx);
    ctx->saveBuffer = lv_textarea_get_text(ctx->uiNoteText);
    saveFile(ctx, ctx->filePath);
}

void onSaveAsPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    hideOverlay(ctx);
    ctx->saveBuffer = lv_textarea_get_text(ctx->uiNoteText);
    ctx->saveFileLaunchId = fileselection::startForExistingOrNewFile(ctx->appInstanceId, ctx->saveResultStream, ctx->saveResultBuffer, sizeof(ctx->saveResultBuffer), ctx->eventGroup);
    LOG_I(TAG, "launched with id %u", ctx->saveFileLaunchId);
}

void onOpenPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    hideOverlay(ctx);
    ctx->loadFileLaunchId = fileselection::startForExistingFile(ctx->appInstanceId, ctx->loadResultStream, ctx->loadResultBuffer, sizeof(ctx->loadResultBuffer), ctx->eventGroup);
    LOG_I(TAG, "launched with id %u", ctx->loadFileLaunchId);
}

void addOverlayButton(Context* ctx, const char* icon, lv_event_cb_t callback) {
    auto* button = lvgl_icon_button_create(ctx->uiOverlay);
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, icon);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, ctx);
}

void showOverlay(Context* ctx) {
    lv_obj_clean(ctx->uiOverlay);
    addOverlayButton(ctx, LVGL_ICON_SHARED_NOTE_ADD, onNewPressed);
    // Saving needs a file, which "save as" picks
    if (!ctx->filePath.empty()) {
        addOverlayButton(ctx, LVGL_ICON_SHARED_SAVE, onSavePressed);
    }
    addOverlayButton(ctx, LVGL_ICON_SHARED_SAVE_AS, onSaveAsPressed);
    addOverlayButton(ctx, LVGL_ICON_SHARED_FOLDER_OPEN, onOpenPressed);

    lv_obj_set_hidden(ctx->uiOverlay, false);
    lv_obj_align_to(ctx->uiOverlay, ctx->uiToolbar, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    lv_group_t* group = lv_group_get_default();
    if (group != nullptr) {
        lv_group_focus_obj(ctx->uiOverlay);
        lv_gridnav_set_focused(ctx->uiOverlay, lv_obj_get_child(ctx->uiOverlay, 0), LV_ANIM_OFF);
        // Touch shows no selection, until a key moves the focus
        lv_indev_t* indev = lv_indev_active();
        if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lvgl_focus_hide_key_selection(group);
        }
    }
}

void onOverflowPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    if (lv_obj_is_hidden(ctx->uiOverlay)) {
        showOverlay(ctx);
    } else {
        hideOverlay(ctx);
    }
}

void onOverlayKey(lv_event_t* e) {
    if (lv_event_get_key(e) == LV_KEY_ESC) {
        hideOverlay(static_cast<Context*>(lv_event_get_user_data(e)));
    }
}

void onNoteTextPressed(lv_event_t* e) {
    hideOverlay(static_cast<Context*>(lv_event_get_user_data(e)));
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_scrollable(parent, false);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    ctx->uiToolbar = lvgl_toolbar_create(parent, "Notes");
    lv_obj_align(ctx->uiToolbar, LV_ALIGN_TOP_MID, 0, 0);
    ctx->uiOverflowButton = lvgl_toolbar_add_text_button_action(ctx->uiToolbar, LVGL_ICON_SHARED_MORE_VERT, onOverflowPressed, ctx);
    lv_obj_set_style_text_font(ctx->uiOverflowButton, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);

    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_height(wrapper, LV_PCT(100));
    lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(wrapper, false);

    ctx->uiNoteText = lv_textarea_create(wrapper);
    lv_obj_set_width(ctx->uiNoteText, LV_PCT(100));
    lv_obj_set_height(ctx->uiNoteText, LV_PCT(86));
    lv_textarea_set_password_mode(ctx->uiNoteText, false);
    if (lv_display_get_color_format(lv_obj_get_display(parent)) != LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_bg_color(ctx->uiNoteText, lv_color_hex(0x262626), LV_PART_MAIN);
    }
    lv_textarea_set_placeholder_text(ctx->uiNoteText, "Notes...");
    lv_obj_add_event_cb(ctx->uiNoteText, onNoteTextPressed, LV_EVENT_PRESSED, ctx);

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
    lv_obj_set_scrollable(footer, false);

    ctx->uiCurrentFileName = lv_label_create(footer);
    lv_label_set_long_mode(ctx->uiCurrentFileName, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(ctx->uiCurrentFileName, LV_SIZE_CONTENT);
    lv_obj_set_height(ctx->uiCurrentFileName, LV_SIZE_CONTENT);
    lv_label_set_text(ctx->uiCurrentFileName, "Untitled");
    lv_obj_align(ctx->uiCurrentFileName, LV_ALIGN_CENTER, 0, 0);

    // Floating over the text area, below the toolbar
    ctx->uiOverlay = lvgl_card_create(parent);
    lv_obj_set_floating(ctx->uiOverlay, true);
    lv_obj_set_size(ctx->uiOverlay, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->uiOverlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->uiOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(ctx->uiOverlay, false);
    lvgl_obj_add_edge_padding(ctx->uiOverlay);
    // The arrow keys move between the overlay's buttons as one row
    lvgl_grid_navigation_add(ctx->uiOverlay);
    lv_obj_add_event_cb(ctx->uiOverlay, onOverlayKey, LV_EVENT_KEY, ctx);
    lv_obj_set_hidden(ctx->uiOverlay, true);

    if (!ctx->filePath.empty()) {
        openFile(ctx, ctx->filePath);
    }
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

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

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
                            auto path = std::string(destination, length);
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
                            auto path = std::string(destination, length);
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
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start(const std::string& filePath) {
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
