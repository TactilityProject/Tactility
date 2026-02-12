#include <Tactility/app/AppManifest.h>
#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/Assets.h>

#include <Tactility/file/File.h>

#include <lvgl.h>
#include <tactility/lvgl_symbols_shared.h>

namespace tt::app::notes {

static const auto LOGGER = Logger("Notes");
constexpr auto* NOTES_FILE_ARGUMENT = "file";

class NotesApp final : public App {

    lv_obj_t* uiCurrentFileName;
    lv_obj_t* uiDropDownMenu;
    lv_obj_t* uiNoteText;

    std::string filePath;
    std::string saveBuffer;

    LaunchId loadFileLaunchId = 0;
    LaunchId saveFileLaunchId = 0;

#pragma region Main_Events_Functions

    void appNotesEventCb(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* obj = lv_event_get_target_obj(e);

        if (code == LV_EVENT_VALUE_CHANGED) {
            if (obj == uiDropDownMenu) {
                switch (lv_dropdown_get_selected(obj)) {
                    case 0: // New
                        resetFileContent();
                        break;
                    case 1: // Save
                        if (!filePath.empty()) {
                            lvgl::getSyncLock()->lock();
                            saveBuffer = lv_textarea_get_text(uiNoteText);
                            lvgl::getSyncLock()->unlock();
                            saveFile(filePath);
                        }
                        break;
                    case 2: // Save as...
                        lvgl::getSyncLock()->lock();
                        saveBuffer = lv_textarea_get_text(uiNoteText);
                        lvgl::getSyncLock()->unlock();
                        saveFileLaunchId = fileselection::startForExistingOrNewFile();
                        LOGGER.info("launched with id {}", saveFileLaunchId);
                        break;
                    case 3: // Load
                        loadFileLaunchId = fileselection::startForExistingFile();
                        LOGGER.info("launched with id {}", loadFileLaunchId);
                        break;
                }
            } else {
                auto* cont = lv_event_get_current_target_obj(e);
                if (obj == cont) return;
                if (lv_obj_get_child(cont, 1)) {
                    saveFileLaunchId = fileselection::startForExistingOrNewFile();
                    LOGGER.info("launched with id {}", saveFileLaunchId);
                } else { //Reset
                    resetFileContent();
                }
            }
        }
    }

    void resetFileContent() {
        lv_textarea_set_text(uiNoteText, "");
        filePath = "";
        saveBuffer = "";
        lv_label_set_text(uiCurrentFileName, "Untitled");
    }

#pragma region Open_Events_Functions

    void openFile(const std::string& path) {
        // We might be reading from the SD card, which could share a SPI bus with other devices (display)
        file::getLock(path)->withLock([this, path] {
            auto data = file::readString(path);
            if (data != nullptr) {
               auto lock = lvgl::getSyncLock()->asScopedLock();
               lock.lock();
               lv_textarea_set_text(uiNoteText, reinterpret_cast<const char*>(data.get()));
               lv_label_set_text(uiCurrentFileName, path.c_str());
               filePath = path;
               LOGGER.info("Loaded from {}", path);
            }
        });
    }

    bool saveFile(const std::string& path) {
        // We might be writing to SD card, which could share a SPI bus with other devices (display)
        bool result = false;
        file::getLock(path)->withLock([&result, this, path] {
           if (file::writeString(path, saveBuffer.c_str())) {
               LOGGER.info("Saved to {}", path);
               filePath = path;
               result = true;
           }
        });
        return result;
    }

#pragma endregion Open_Events_Functions

    void onCreate(AppContext& appContext) override {
        auto parameters = appContext.getParameters();
        std::string file_path;
        if (parameters != nullptr && parameters->optString(NOTES_FILE_ARGUMENT, file_path)) {
            if (!file_path.empty()) {
                filePath = file_path;
            }
        }
    }
    void onShow(AppContext& context, lv_obj_t* parent) override {
        lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lv_obj_t* toolbar = lvgl::toolbar_create(parent, context);
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        uiDropDownMenu = lv_dropdown_create(toolbar);
        lv_dropdown_set_options(uiDropDownMenu, LV_SYMBOL_FILE " New File\n" LV_SYMBOL_SAVE " Save\n" LV_SYMBOL_SAVE " Save As...\n" LV_SYMBOL_DIRECTORY " Open File");
        lv_dropdown_set_text(uiDropDownMenu, "Menu");
        lv_dropdown_set_symbol(uiDropDownMenu, LV_SYMBOL_DOWN);
        lv_dropdown_set_selected_highlight(uiDropDownMenu, false);
        lv_obj_set_style_border_color(uiDropDownMenu, lv_color_hex(0xFAFAFA), LV_PART_MAIN);
        lv_obj_set_style_border_width(uiDropDownMenu, 1, LV_PART_MAIN);
        lv_obj_align(uiDropDownMenu, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(uiDropDownMenu,
            [](lv_event_t* e) {
                auto *self = static_cast<NotesApp *>(lv_event_get_user_data(e));
                self->appNotesEventCb(e);
            },
            LV_EVENT_VALUE_CHANGED,
            this
        );

        lv_obj_t* wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_height(wrapper, LV_PCT(100));
        lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(wrapper, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(wrapper, 0, 0);
        lv_obj_remove_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);

        uiNoteText = lv_textarea_create(wrapper);
        lv_obj_set_width(uiNoteText, LV_PCT(100));
        lv_obj_set_height(uiNoteText, LV_PCT(86));
        lv_textarea_set_password_mode(uiNoteText, false);
        lv_obj_set_style_bg_color(uiNoteText, lv_color_hex(0x262626), LV_PART_MAIN);
        lv_textarea_set_placeholder_text(uiNoteText, "Notes...");

        lv_obj_t* footer = lv_obj_create(wrapper);
        lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x262626), LV_PART_MAIN);
        lv_obj_set_width(footer, LV_PCT(100));
        lv_obj_set_height(footer, LV_PCT(14));
        lv_obj_set_style_pad_all(footer, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

        uiCurrentFileName = lv_label_create(footer);
        lv_label_set_long_mode(uiCurrentFileName, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_width(uiCurrentFileName, LV_SIZE_CONTENT);
        lv_obj_set_height(uiCurrentFileName, LV_SIZE_CONTENT);
        lv_label_set_text(uiCurrentFileName, "Untitled");
        lv_obj_align(uiCurrentFileName, LV_ALIGN_CENTER, 0, 0);

        if (!filePath.empty()) {
            openFile(filePath);
        }
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> resultData) override {
        LOGGER.info("Result for launch id {}", launchId);
        if (launchId == loadFileLaunchId) {
            loadFileLaunchId = 0;
            if (result == Result::Ok && resultData != nullptr) {
                auto path = fileselection::getResultPath(*resultData);
                openFile(path);
            }
        } else if (launchId == saveFileLaunchId) {
            saveFileLaunchId = 0;
            if (result == Result::Ok && resultData != nullptr) {
                auto path = fileselection::getResultPath(*resultData);
                // Must re-open file, because UI was cleared after opening other app
                if (saveFile(path)) {
                    openFile(path);
                }
            }
        }
    }
};

extern const AppManifest manifest = {
    .appId = "Notes",
    .appName = "Notes",
    .appIcon = LVGL_SYMBOL_EDIT_NOTE,
    .createApp = create<NotesApp>
};

LaunchId start(const std::string& filePath) {
    auto parameters = std::make_shared<Bundle>();
    parameters->putString(NOTES_FILE_ARGUMENT, filePath);
    return app::start(manifest.appId, parameters);
}

} // namespace tt::app::notes