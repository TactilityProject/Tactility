#include <tactility/filesystem/fs.h>
#include <app/event.h>
#include <tactility/check.h>
#include <app/execute.h>
#include <app/install.h>
#include <app/start.h>
#include <app/stream.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/insets.h>
#include <lvgl/lvgl.h>
#include <lvgl/svg.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/files.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/path_bar.h>
#include <lvgl/widgets/toolbar.h>

#include <generated_assets.h>

#include <Tactility/app/files/SupportedFiles.h>
#include <Tactility/app/dialog/Dialog.h>
#include <Tactility/app/imageviewer/ImageViewer.h>
#include <Tactility/app/notes/Notes.h>
#include <Tactility/file/File.h>
#include <Tactility/StringUtils.h>
#include <Tactility/i18n/Localisation.h>

#define L10N_APP "files"
#include <Tactility/i18n/AppDomain.h>

#include <tactility/device.h>
#include <tactility/file_ops.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/log.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <unistd.h>
#include <vector>

// Included last, as it makes std:: resolve to tt::std
#include <Tactility/app/files/View.h>

namespace tt::app::files {

constexpr auto* TAG = "Files";

// region Callbacks

static void onBackPressedCallback(lv_event_t* event) {
    auto* view = static_cast<files::View*>(lv_event_get_user_data(event));
    view->onBackPressed();
}

static void onOverflowPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onOverflowPressed();
}

// endregion

// region File helpers

static bool isUsbMountPoint(const char* name) {
    return strncmp(name, "usb", 3) == 0 && isdigit((unsigned char)name[3]);
}

enum class FileIcon { Folder, SdCard, Usb, Link, Image, Text, App, Executable, File, Unknown };

struct FileIconAsset {
    const uint8_t* colour;
    size_t colourSize;
    const uint8_t* mono;
    size_t monoSize;
};

#define FILE_ICON_ASSET(name) { asset_file_icon_##name##_svg, asset_file_icon_##name##_svg_SIZE, asset_file_icon_##name##_mono_svg, asset_file_icon_##name##_mono_svg_SIZE }

// Indexed by FileIcon, Unknown has no icon
constexpr FileIconAsset FILE_ICON_ASSETS[] = {
    FILE_ICON_ASSET(folder),
    FILE_ICON_ASSET(sd_card),
    FILE_ICON_ASSET(usb),
    FILE_ICON_ASSET(link),
    FILE_ICON_ASSET(image),
    FILE_ICON_ASSET(text),
    FILE_ICON_ASSET(app),
    FILE_ICON_ASSET(executable),
    FILE_ICON_ASSET(file),
};

// The icon font glyphs shown instead of the SVG icons, indexed by FileIcon
constexpr const char* FILE_ICON_GLYPHS[] = {
    LVGL_ICON_SHARED_FOLDER,
    LVGL_ICON_SHARED_SD_CARD,
    LVGL_ICON_SHARED_USB,
    LVGL_ICON_SHARED_LINK,
    LVGL_ICON_SHARED_IMAGE,
    LVGL_ICON_SHARED_DESCRIPTION,
    LVGL_ICON_SHARED_DEPLOYED_CODE,
    LVGL_ICON_SHARED_PLAY_ARROW,
    LVGL_ICON_SHARED_DRAFT,
};

// Returns Unknown for the files that View::resolveImage() checks, as that reads the file
static FileIcon getIcon(const dirent& entry, bool isRoot) {
    if (entry.d_type == file::TT_DT_DIR || entry.d_type == file::TT_DT_CHR) {
        if (isRoot && strncmp(entry.d_name, "sd", 2) == 0) {
            return FileIcon::SdCard;
        } else if (isRoot && isUsbMountPoint(entry.d_name)) {
            return FileIcon::Usb;
        } else {
            return FileIcon::Folder;
        }
    } else if (entry.d_type == file::TT_DT_LNK) {
        return FileIcon::Link;
    } else if (isSupportedImageFile(entry.d_name)) {
        return FileIcon::Image;
    } else if (isSupportedTextFile(entry.d_name)) {
        return FileIcon::Text;
    } else if (isSupportedAppFile(entry.d_name)) {
        return FileIcon::App;
    } else {
        return FileIcon::Unknown;
    }
}

static bool copyFileContents(const ::std::string& src, const ::std::string& dst) {
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

static bool copyRecursive(const ::std::string& src, const ::std::string& dst) {
    if (directory_exists(src.c_str())) {
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

            ::std::string name = entry->d_name; // copy before releasing lock

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

namespace {

struct RunJob {
    uint32_t appInstanceId;
    ::std::string path;
};

// Starting an executable loads it from a file
void runWork(void* context) {
    auto* job = static_cast<RunJob*>(context);
    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(job->path.c_str()) };
    AppInstanceId instance_id = 0;
    AppStartContext start_context = app_start_context_for_location(location);
    if (app_start_with_context(&start_context, &instance_id) != ERROR_NONE) {
        LOG_W(TAG, "Failed to run %s", job->path.c_str());
        dialog::alert(job->appInstanceId, tr("Run failed"), i18n::format(tr("Could not run \"{}\"."), { file::getLastPathSegment(job->path) }));
    }
    delete job;
}

} // namespace

void View::beginOperation() {
    pendingOperations++;
    updateBusySpinner();
}

void View::endOperation() {
    pendingOperations--;
    updateBusySpinner();
}

void View::updateBusySpinner() {
    lvgl_lock();
    if (busy_spinner != nullptr) {
        lv_obj_set_hidden(busy_spinner, pendingOperations == 0);
    }
    lvgl_unlock();
}

void View::navigateTo(const ::std::string& path, bool firstPage, bool selectFirst) {
    if (files == nullptr) {
        return;
    }
    if (lvgl_files_set_path(files, path.c_str(), firstPage, selectFirst) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue reading %s", path.c_str());
        return;
    }
    pendingPath = path;
    if (!navigationPending) {
        navigationPending = true;
        beginOperation();
    }
}

void View::reload() {
    lvgl_lock();
    if (files != nullptr) {
        // A read of the shown directory would replace a pending navigation
        if (navigationPending && pendingPath != state->getCurrentPath()) {
            refreshDeferred = true;
        } else {
            navigateTo(state->getCurrentPath(), false, false);
        }
    }
    lvgl_unlock();
}

void View::onLoaded(lv_obj_t*, const char* path, error_t error, void* userData) {
    static_cast<View*>(userData)->onDirectoryLoaded(path, error);
}

void View::onDirectoryLoaded(const ::std::string& path, error_t error) {
    if (navigationPending && !lvgl_files_is_loading(files)) {
        navigationPending = false;
        endOperation();
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to read %s", path.c_str());
        // The shown directory stays, so it gets the refresh that waited for this navigation
        if (refreshDeferred && !lvgl_files_is_loading(files)) {
            refreshDeferred = false;
            navigateTo(state->getCurrentPath(), false, false);
            return;
        }
        // The data directory doesn't exist yet, e.g. without an SD card
        if (!state->isLoaded() && path != "/") {
            navigateTo("/", true, !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
        }
        return;
    }
    refreshDeferred = false;
    LOG_I(TAG, "%s has %u entries", path.c_str(), static_cast<unsigned>(lvgl_files_get_entry_count(files)));
    if (path != state->getCurrentPath()) {
        // The selection belongs to the previous directory
        clearSelection();
    }
    state->setCurrentPath(path);
    lv_obj_set_hidden(overflow_button, path == "/");
    // Also replaces the fallback of the selection, the nearest segment, for when the directory has no entries
    lvgl_path_bar_set_path(path_bar, path.c_str());
    // The pressed segment is gone, so the selection moves to the segment below the new directory
    if (selectParentSegment) {
        selectParentSegment = false;
        lvgl_path_bar_focus_parent_segment(path_bar);
    }
}

namespace {

void onAlertClosed(int32_t button, void* userData) {
    static_cast<View*>(userData)->onDialogClosed(button, "");
}

void onInputClosed(bool confirmed, const char* text, void* userData) {
    static_cast<View*>(userData)->onDialogClosed(confirmed ? 0 : 1, text);
}

void onMenuClosed(int32_t item, void* userData) {
    static_cast<View*>(userData)->onDialogClosed(item, "");
}

} // namespace

void View::onDialogClosed(int32_t result, const char* text) {
    dialogResult = result;
    dialogText = text;
    task_event_group_signal(eventGroup, dialogBit);
}

void View::viewFile(const ::std::string& path, const ::std::string& filename) {
    ::std::string file_path = path + "/" + filename;
    LOG_I(TAG, "Clicked %s", file_path.c_str());

    if (isSupportedAppFile(filename)) {
        if (dialogOwner != DialogOwner::None) {
            return;
        }
        // install(filename);
        auto message = i18n::format(tr("Do you want to install {}?"), { filename });
        installAppPath = file_path;
        dialogOwner = DialogOwner::Install;
        if (dialog::alert(appInstanceId, tr("Install?"), message, { tr("Yes"), tr("No") }, onAlertClosed, this) != ERROR_NONE) {
            dialogOwner = DialogOwner::None;
        }
    } else if (isSupportedImageFile(filename)) {
        imageviewer::start(file_path);
    } else if (isSupportedTextFile(filename)) {
        notes::start(file_path);
    } else if (isExecutable(filename)) {
        runFile(file_path);
    } else {
        LOG_W(TAG, "Opening files of this type is not supported");
    }

    onNavigate();
}

void View::runFile(const ::std::string& file_path) {
    LOG_I(TAG, "Running %s", file_path.c_str());

    if (!isExecutable(file::getLastPathSegment(file_path))) {
        LOG_W(TAG, "Not executable: %s", file_path.c_str());
        dialog::alert(appInstanceId, tr("Run failed"), i18n::format(tr("\"{}\" is not an executable."), { file::getLastPathSegment(file_path) }));
        return;
    }

    auto* job = new (::std::nothrow) RunJob { appInstanceId, file_path };
    if (job == nullptr || file_ops_queue(runWork, job, nullptr) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue running %s", file_path.c_str());
        delete job;
    }
}

View::View(const ::std::shared_ptr<State>& state, TaskEventGroup* eventGroup) :
    state(state),
    eventGroup(eventGroup) {
    check(task_event_group_claim_bit(eventGroup, &pasteBit) == ERROR_NONE);
    check(task_event_group_claim_bit(eventGroup, &dialogBit) == ERROR_NONE);
}

void View::loadIcons() {
    if (!areSvgIconsEnabled()) {
        return;
    }

    const auto size = static_cast<uint32_t>(lvgl_get_shared_icon_large_font_height());
    const bool preferMono = lvgl_theme_is_mono();
    for (size_t i = 0; i < ::std::size(FILE_ICON_ASSETS); i++) {
        const auto& asset = FILE_ICON_ASSETS[i];
        icons[i] = lvgl_svg_rasterize_variants(
            reinterpret_cast<const char*>(asset.colour), asset.colourSize,
            reinterpret_cast<const char*>(asset.mono), asset.monoSize,
            preferMono, size, &iconRecolor[i]
        );
        if (icons[i] == nullptr) {
            LOG_W(TAG, "Failed to rasterize file icon %d", static_cast<int>(i));
        }
    }
}

View::~View() {
    for (auto* icon : icons) {
        if (icon != nullptr) lv_draw_buf_destroy(icon);
    }
    task_event_group_release_bit(eventGroup, pasteBit);
    task_event_group_release_bit(eventGroup, dialogBit);
}

uint32_t View::classifyEntry(const char* directory, const dirent* entry, void*) {
    auto icon = getIcon(*entry, strcmp(directory, "/") == 0);
    if (icon == FileIcon::Unknown) {
        // Reads the file, which is fine on the file operations task
        const ::std::string child_path = file::getChildPath(directory, entry->d_name);
        AppLocation location { APP_LOCATION_PATH, const_cast<char*>(child_path.c_str()) };
        icon = app_is_executable(location) ? FileIcon::Executable : FileIcon::File;
    }
    return static_cast<uint32_t>(icon);
}

void View::getEntryIcon(lv_obj_t*, const dirent*, uint32_t kind, LvglFilesIcon* icon, void* userData) {
    auto* view = static_cast<View*>(userData);
    if (kind >= ::std::size(FILE_ICON_GLYPHS)) {
        return;
    }
    // The glyph is shown when there is no image: without PSRAM or when it failed to rasterize
    icon->glyph = FILE_ICON_GLYPHS[kind];
    icon->image = reinterpret_cast<const lv_image_dsc_t*>(view->icons[kind]);
    icon->image_recolor = view->iconRecolor[kind];
}

void View::onEntryClicked(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
    static_cast<View*>(userData)->onDirEntryPressed(entry->d_name);
}

void View::onEntryLongPressed(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
    static_cast<View*>(userData)->onDirEntryLongPressed(entry->d_name);
}

void View::onEntryKey(lv_obj_t*, const dirent* entry, uint32_t, uint32_t key, void* userData) {
    static_cast<View*>(userData)->onDirEntryKeyPressed(entry->d_name, key);
}

bool View::isEntryChecked(lv_obj_t*, const dirent* entry, uint32_t, void* userData) {
    // The selection is kept when the tiles are rebuilt, e.g. on another page
    return static_cast<View*>(userData)->state->isSelectedEntry(entry->d_name);
}

bool View::isExecutable(const ::std::string& name) const {
    uint32_t kind = 0;
    return files != nullptr && lvgl_files_find_entry(files, name.c_str(), nullptr, &kind) && kind == static_cast<uint32_t>(FileIcon::Executable);
}

bool View::findDirent(const ::std::string& name, dirent& out_entry) const {
    return files != nullptr && lvgl_files_find_entry(files, name.c_str(), &out_entry, nullptr);
}

void View::onDirEntryPressed(const ::std::string& name) {
    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }

    // In the selection mode, a tap adds an entry to the selection or removes it
    if (isSelecting()) {
        toggleSelectedEntry(dir_entry);
        return;
    }

    LOG_I(TAG, "Pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);
    openEntry(dir_entry);
}

void View::openEntry(const dirent& dir_entry) {
    const ::std::string name = dir_entry.d_name;
    state->setSelectedChildEntry(name);

    using namespace tt::file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR: {
            lv_obj_t* tile = lvgl_files_find_tile(files, name.c_str());
            // A touch doesn't show a selection. Without a pointer device there is no touch, so a selection is always wanted.
            const bool from_keys = tile != nullptr && lv_obj_has_state(tile, LV_STATE_FOCUS_KEY);
            navigateTo(file::getChildPath(state->getCurrentPath(), name), true, from_keys || !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
            break;
        }

        case TT_DT_LNK:
            LOG_W(TAG, "opening links is not supported");
            break;

        default:
            viewFile(state->getCurrentPath(), name);
            onNavigate();
            break;
    }
}

bool View::isSelecting() const {
    // At the root only mount points have actions, so they can't be combined
    return selectionMode && state->getCurrentPath() != "/" && !state->getSelectedEntries().empty();
}

void View::setTileChecked(const ::std::string& name, bool checked) {
    lv_obj_t* tile = files != nullptr ? lvgl_files_find_tile(files, name.c_str()) : nullptr;
    if (tile != nullptr) {
        lv_obj_set_state(tile, LV_STATE_CHECKED, checked);
    }
}

void View::toggleSelectedEntry(const dirent& dir_entry) {
    if (dir_entry.d_type == file::TT_DT_LNK) {
        return;
    }
    const ::std::string name = dir_entry.d_name;
    setTileChecked(name, state->toggleSelectedEntry(name));
    const auto entries = state->getSelectedEntries();
    if (entries.empty()) {
        clearSelection();
    } else {
        state->setSelectedChildEntry(entries.front());
    }
}

void View::clearSelection() {
    selectionMode = false;
    for (const auto& name : state->getSelectedEntries()) {
        setTileChecked(name, false);
    }
    state->clearSelectedEntries();
}

void View::onDirEntryLongPressed(const ::std::string& name) {
    if (dialogOwner != DialogOwner::None) {
        return;
    }

    dirent dir_entry;
    if (!findDirent(name, dir_entry) || dir_entry.d_type == file::TT_DT_LNK) {
        return;
    }

    LOG_I(TAG, "Long-pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);

    // In the selection mode, the menu is for all selected entries, including the pressed one
    if (isSelecting()) {
        if (!state->isSelectedEntry(name)) {
            toggleSelectedEntry(dir_entry);
        }
        showSelectionMenu();
        return;
    }

    clearSelection();
    showEntryMenu(dir_entry);
}

void View::showEntryMenu(const dirent& dir_entry) {
    const ::std::string name = dir_entry.d_name;
    const bool is_directory = dir_entry.d_type == file::TT_DT_DIR || dir_entry.d_type == file::TT_DT_CHR;
    ::std::vector<MenuAction> actions;

    if (state->getCurrentPath() == "/") {
        // At root, only USB mount points support actions (eject).
        // Other root-level entries intentionally have no context actions.
        if (!isUsbMountPoint(dir_entry.d_name)) {
            return;
        }
        actions = { MenuAction::Open, MenuAction::Eject };
    } else {
        const bool can_open = is_directory || isSupportedAppFile(name) || isSupportedImageFile(name) || isSupportedTextFile(name) || isExecutable(name);
        if (can_open) {
            actions.push_back(MenuAction::Open);
        }
        actions.push_back(MenuAction::Select);
        actions.push_back(MenuAction::Copy);
        actions.push_back(MenuAction::Cut);
        if (state->hasClipboard()) {
            actions.push_back(MenuAction::Paste);
        }
        actions.push_back(MenuAction::Rename);
        actions.push_back(MenuAction::Delete);
    }

    // The tile shows which entry the menu is for
    state->setSelectedChildEntry(name);
    state->selectOnlyEntry(name);
    setTileChecked(name, true);
    showMenu(name, actions);
}

void View::showSelectionMenu() {
    const auto entries = state->getSelectedEntries();
    ::std::vector<MenuAction> actions;
    if (entries.size() == 1) {
        actions.push_back(MenuAction::Open);
    }
    actions.push_back(MenuAction::Copy);
    actions.push_back(MenuAction::Cut);
    if (state->hasClipboard()) {
        actions.push_back(MenuAction::Paste);
    }
    if (entries.size() == 1) {
        actions.push_back(MenuAction::Rename);
    }
    actions.push_back(MenuAction::Delete);
    actions.push_back(MenuAction::DeselectAll);
    const auto title = entries.size() == 1 ? entries.front() : i18n::format(tr("{} selected"), { ::std::to_string(entries.size()) });
    showMenu(title, actions);
}

void View::showMenu(const ::std::string& title, const ::std::vector<MenuAction>& actions) {
    if (dialogOwner != DialogOwner::None) {
        return;
    }
    ::std::vector<::std::string> labels;
    ::std::vector<const char*> icons;
    for (const auto action : actions) {
        switch (action) {
            case MenuAction::Open: labels.emplace_back(tr("Open")); icons.push_back(LVGL_ICON_SHARED_FOLDER_OPEN); break;
            case MenuAction::Select: labels.emplace_back(tr("Select")); icons.push_back(LVGL_ICON_SHARED_CHECK); break;
            case MenuAction::DeselectAll: labels.emplace_back(tr("Deselect all")); icons.push_back(LVGL_ICON_SHARED_CLOSE); break;
            case MenuAction::Copy: labels.emplace_back(tr("Copy")); icons.push_back(LVGL_ICON_SHARED_CONTENT_COPY); break;
            case MenuAction::Cut: labels.emplace_back(tr("Cut")); icons.push_back(LVGL_ICON_SHARED_CONTENT_CUT); break;
            case MenuAction::Paste: labels.emplace_back(tr("Paste")); icons.push_back(LVGL_ICON_SHARED_CONTENT_PASTE); break;
            case MenuAction::Rename: labels.emplace_back(tr("Rename")); icons.push_back(LVGL_ICON_SHARED_EDIT); break;
            case MenuAction::Delete: labels.emplace_back(tr("Delete")); icons.push_back(LVGL_ICON_SHARED_DELETE); break;
            case MenuAction::Eject: labels.emplace_back(tr("Eject")); icons.push_back(LVGL_ICON_SHARED_EJECT); break;
            case MenuAction::NewFile: labels.emplace_back(tr("New File")); icons.push_back(LVGL_ICON_SHARED_NOTE_ADD); break;
            case MenuAction::NewFolder: labels.emplace_back(tr("New Folder")); icons.push_back(LVGL_ICON_SHARED_CREATE_NEW_FOLDER); break;
        }
    }
    menuActions = actions;
    dialogOwner = DialogOwner::Menu;
    if (dialog::selection(appInstanceId, title, labels, icons, onMenuClosed, this) != ERROR_NONE) {
        dialogOwner = DialogOwner::None;
        if (!selectionMode) {
            clearSelection();
        }
    }
}

void View::runMenuAction(MenuAction action) {
    switch (action) {
        case MenuAction::Open: {
            dirent dir_entry;
            const auto name = state->getSelectedChildEntry();
            clearSelection();
            if (findDirent(name, dir_entry)) {
                openEntry(dir_entry);
            }
            break;
        }
        case MenuAction::Select:
            // The entry of the menu stays selected
            selectionMode = true;
            break;
        case MenuAction::DeselectAll:
            clearSelection();
            break;
        case MenuAction::Copy:
            onCopyPressed();
            break;
        case MenuAction::Cut:
            onCutPressed();
            break;
        case MenuAction::Paste:
            onPastePressed();
            break;
        case MenuAction::Rename:
            onRenamePressed();
            break;
        case MenuAction::Delete:
            onDeletePressed();
            break;
        case MenuAction::Eject:
            onEjectPressed();
            break;
        case MenuAction::NewFile:
            onNewFilePressed();
            break;
        case MenuAction::NewFolder:
            onNewFolderPressed();
            break;
    }
}

void View::onDirEntryKeyPressed(const ::std::string& name, uint32_t key) {
    // Space shows the menu like a long press
    if (key == ' ') {
        onDirEntryLongPressed(name);
        return;
    }

    if (isSelecting()) {
        switch (key) {
            case LV_KEY_ESC:
                clearSelection();
                break;
            case 'c': case 'C':
                onCopyPressed();
                break;
            case 'x': case 'X':
                onCutPressed();
                break;
            case 'v': case 'V':
                if (state->hasClipboard()) onPastePressed();
                break;
            case 'r': case 'R':
                if (state->getSelectedEntries().size() == 1) onRenamePressed();
                break;
            case LV_KEY_DEL:
            case LV_KEY_BACKSPACE:
                onDeletePressed();
                break;
            default:
                break;
        }
        return;
    }

    // Keyboards report their delete key as either delete or backspace (e.g. Cardputer's "del" key)
    const bool is_delete = key == LV_KEY_DEL || key == LV_KEY_BACKSPACE;
    const bool is_rename = key == 'r' || key == 'R';
    if (!is_delete && !is_rename) {
        return;
    }

    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }
    // Same rules as the long-press actions: root entries are mount points, links have no actions
    if (state->getCurrentPath() == "/" || dir_entry.d_type == file::TT_DT_LNK) {
        return;
    }
    clearSelection();
    state->setSelectedChildEntry(dir_entry.d_name);
    if (is_delete) {
        onDeletePressed();
    } else {
        onRenamePressed();
    }
}

void View::onBackPressed() {
    app_event_emit_close(appInstanceId);
}

void View::onOverflowPressed() {
    ::std::vector<MenuAction> actions = { MenuAction::NewFile, MenuAction::NewFolder };
    if (state->hasClipboard()) {
        actions.push_back(MenuAction::Paste);
    }
    showMenu(file::getLastPathSegment(state->getCurrentPath()), actions);
}

void View::onRenamePressed() {
    onNavigate();
    if (dialogOwner != DialogOwner::None) {
        return;
    }
    ::std::string entry_name = state->getSelectedChildEntry();
    LOG_I(TAG, "Pending rename %s", entry_name.c_str());
    renameTargetPath = state->getSelectedChildPath();
    state->setPendingAction(State::ActionRename);
    dialogOwner = DialogOwner::Action;
    if (dialog::input(appInstanceId, tr("Rename"), "", entry_name, onInputClosed, this) != ERROR_NONE) {
        dialogOwner = DialogOwner::None;
        state->setPendingAction(State::ActionNone);
    }
}

void View::onDeletePressed() {
    // Before onNavigate(), which clears the selection
    const auto paths = getSelectedPaths();
    onNavigate();
    if (dialogOwner != DialogOwner::None) {
        return;
    }
    state->setPendingDeletePaths(paths);
    state->setPendingAction(State::ActionDelete);
    ::std::string message;
    if (paths.size() == 1) {
        LOG_I(TAG, "Pending delete %s", paths[0].c_str());
        message = i18n::format(tr("Do you want to delete this?\n{}"), { paths[0] });
    } else {
        LOG_I(TAG, "Pending delete of %u items", static_cast<unsigned>(paths.size()));
        message = i18n::format(tr("Do you want to delete these {} items?"), { ::std::to_string(paths.size()) });
    }
    dialogOwner = DialogOwner::Action;
    if (dialog::alert(appInstanceId, tr("Are you sure?"), message, { tr("Yes"), tr("No") }, onAlertClosed, this) != ERROR_NONE) {
        dialogOwner = DialogOwner::None;
        state->setPendingAction(State::ActionNone);
    }
}

void View::onNewFilePressed() {
    onNavigate();
    if (dialogOwner != DialogOwner::None) {
        return;
    }
    LOG_I(TAG, "Creating new file");
    state->setPendingAction(State::ActionCreateFile);
    dialogOwner = DialogOwner::Action;
    if (dialog::input(appInstanceId, tr("New File"), tr("Enter filename:"), "", onInputClosed, this) != ERROR_NONE) {
        dialogOwner = DialogOwner::None;
        state->setPendingAction(State::ActionNone);
    }
}

void View::onNewFolderPressed() {
    onNavigate();
    if (dialogOwner != DialogOwner::None) {
        return;
    }
    LOG_I(TAG, "Creating new folder");
    state->setPendingAction(State::ActionCreateFolder);
    dialogOwner = DialogOwner::Action;
    if (dialog::input(appInstanceId, tr("New Folder"), tr("Enter folder name:"), "", onInputClosed, this) != ERROR_NONE) {
        dialogOwner = DialogOwner::None;
        state->setPendingAction(State::ActionNone);
    }
}

::std::vector<::std::string> View::getSelectedPaths() const {
    ::std::vector<::std::string> paths;
    const auto current_path = state->getCurrentPath();
    for (const auto& name : state->getSelectedEntries()) {
        paths.push_back(file::getChildPath(current_path, name));
    }
    if (paths.empty()) {
        paths.push_back(state->getSelectedChildPath());
    }
    return paths;
}

void View::onEjectPressed() {
    ::std::string mount_path = state->getSelectedChildPath();
    LOG_I(TAG, "Ejecting %s", mount_path.c_str());

    // One device per mounted drive. Ejecting happens outside the iteration, which holds the device ledger lock.
    ::std::vector<Device*> msc_devices;
    device_for_each_of_type(&USB_HOST_MSC_TYPE, &msc_devices, [](Device* device, void* context) -> bool {
        if (device_is_ready(device) && device_get(device) == ERROR_NONE) {
            static_cast<::std::vector<Device*>*>(context)->push_back(device);
        }
        return true;
    });

    bool ejected = false;
    for (auto* msc_dev : msc_devices) {
        if (!ejected) {
            ejected = usb_msc_eject(msc_dev, mount_path.c_str());
        }
        device_put(msc_dev);
    }

    if (!ejected) {
        LOG_W(TAG, "usb_msc_eject: %s not found", mount_path.c_str());
        dialog::alert(appInstanceId, tr("Eject failed"), i18n::format(tr("Could not eject \"{}\"."), { file::getLastPathSegment(mount_path) }));
    }

    onNavigate();
    navigateTo(state->getCurrentPath(), false, false);
}

void View::onPathSegmentPressed(lv_obj_t*, const char* path, bool fromKeys, void* userData) {
    auto* view = static_cast<View*>(userData);
    view->onNavigate();
    view->selectParentSegment = fromKeys;
    view->navigateTo(path, true, false);
}

// The grid can still hold a deleted segment, as the fallback of a selection that waits for the repopulate
void View::onPathBarRebuilt(lv_obj_t* bar, void* userData) {
    auto* view = static_cast<View*>(userData);
    if (view->files != nullptr) {
        lvgl_tile_grid_replace_empty_fallback(lvgl_files_get_tile_grid(view->files), lvgl_path_bar_get_parent_segment(bar));
    }
}

void View::init(uint32_t appInstanceId, lv_obj_t* parent) {
    this->appInstanceId = appInstanceId;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    const bool is_root = (state->getCurrentPath() == "/");
    // The path bar takes the place of the title
    toolbar = lvgl_toolbar_create(parent, "");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressedCallback, this);
    busy_spinner = lvgl_toolbar_add_spinner_action(toolbar);
    lv_obj_set_hidden(busy_spinner, pendingOperations == 0);
    overflow_button = lvgl_toolbar_add_text_button_action(toolbar, LVGL_ICON_SHARED_MORE_VERT, onOverflowPressedCallback, this);
    lv_obj_set_style_text_font(overflow_button, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_obj_set_hidden(overflow_button, is_root);
    path_bar = lvgl_path_bar_create(toolbar, onPathSegmentPressed, onPathBarRebuilt, this);
    lvgl_path_bar_set_path(path_bar, state->getCurrentPath().c_str());

    const LvglFilesConfig config = {
        .callbacks = {
            .classify = classifyEntry,
            .get_icon = getEntryIcon,
            .is_checked = isEntryChecked,
            .on_clicked = onEntryClicked,
            .on_long_pressed = onEntryLongPressed,
            .on_key = onEntryKey,
            .on_page_changed = nullptr,
            .on_loaded = onLoaded,
            .user_data = this,
        },
        .swipe_navigation = true,
        .paging = getSystemBarsPaging(),
        .page = page,
    };
    files = lvgl_files_create(parent, &config);
    // The new widget reads the directory again. Without a pointer device, keys need a selection to start from.
    navigateTo(state->getCurrentPath(), !state->isLoaded(), !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));

}

void View::destroyWidgets() {
    // The checked tiles are gone, so nothing may stay selected
    state->clearSelectedEntries();
    selectionMode = false;
    busy_spinner = nullptr;
    toolbar = nullptr;
    path_bar = nullptr;
    selectParentSegment = false;
    overflow_button = nullptr;
    // The read of the deleted widget won't report back
    if (navigationPending) {
        navigationPending = false;
        endOperation();
    }
    refreshDeferred = false;
    page = lvgl_tile_grid_get_page(lvgl_files_get_tile_grid(files));
    files = nullptr;
}

void View::onNavigate() {
    clearSelection();
}

void View::onDialogResult() {
    const DialogOwner owner = dialogOwner;
    const int32_t result = dialogResult;
    const ::std::string installPath = installAppPath;
    dialogOwner = DialogOwner::None;
    if (owner == DialogOwner::Menu) {
        lvgl_lock();
        // The widgets are gone while another window covers this one
        if (toolbar == nullptr) {
            state->clearSelectedEntries();
            selectionMode = false;
        } else if (result >= 0 && static_cast<size_t>(result) < menuActions.size()) {
            runMenuAction(menuActions[result]);
        } else if (!selectionMode) {
            // Dismissed: the entry that the menu was for isn't selected anymore
            clearSelection();
        }
        lvgl_unlock();
        return;
    }
    if (owner == DialogOwner::Install) {
        if (result == 0) {
            app_install(installPath.c_str());
        }
        return;
    }

    ::std::string filepath = state->getSelectedChildPath();
    LOG_I(TAG, "Result for %s", filepath.c_str());

    // The entered text of a text dialog (rename/new file/new folder). Empty for Cancel.
    // Other pending actions (delete/paste confirmations) only look at `result`, not `resultText`.
    ::std::string resultText;
    if (result == 0) {
        resultText = dialogText;
    }

    // The answer of the overwrite confirmation counts even when the pending action was reset meanwhile
    const auto action = owner == DialogOwner::Paste ? State::ActionPaste : state->getPendingAction();
    switch (action) {
        case State::ActionDelete: {
            if (result == 0) {
                OperationScope operation(*this);
                for (const auto& path : state->getPendingDeletePaths()) {
                    if (directory_exists(path.c_str())) {
                        if (!file::deleteRecursively(path)) {
                            LOG_W(TAG, "Failed to delete %s", path.c_str());
                        }
                    } else if (file_exists(path.c_str())) {
                        if (remove(path.c_str()) != 0) {
                            LOG_W(TAG, "Failed to delete %s", path.c_str());
                        }
                    }
                }
                state->setPendingDeletePaths({});

                reload();
            }
            break;
        }
        case State::ActionRename: {
            ::std::string new_name = resultText;
            // The entry that was chosen when the dialog opened, not the selection of now
            filepath = renameTargetPath;
            if (!new_name.empty() && new_name != file::getLastPathSegment(filepath)) {
                ::std::string rename_to = file::getChildPath(state->getCurrentPath(), new_name);
                struct stat st;
                if (stat(rename_to.c_str(), &st) == 0) {
                    LOG_W(TAG, "Rename: destination already exists: \"%s\"", rename_to.c_str());
                    state->setPendingAction(State::ActionNone);
                    dialog::alert(appInstanceId, tr("Rename failed"), i18n::format(tr("\"{}\" already exists."), { new_name }));
                    break;
                }
                if (rename(filepath.c_str(), rename_to.c_str()) == 0) {
                    LOG_I(TAG, "Renamed \"%s\" to \"%s\"", filepath.c_str(), rename_to.c_str());
                } else {
                    LOG_E(TAG, "Failed to rename \"%s\" to \"%s\"", filepath.c_str(), rename_to.c_str());
                }

                reload();
            }
            break;
        }
        case State::ActionCreateFile: {
            ::std::string filename = resultText;
            if (!filename.empty()) {
                ::std::string new_file_path = file::getChildPath(state->getCurrentPath(), filename);

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

                reload();
            }
            break;
        }
        case State::ActionCreateFolder: {
            ::std::string foldername = resultText;
            if (!foldername.empty()) {
                ::std::string new_folder_path = file::getChildPath(state->getCurrentPath(), foldername);

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

                reload();
            }
            break;
        }
        case State::ActionPaste: {
            // Only the answer of the confirmation that belongs to the pending paste counts
            if (owner != DialogOwner::Paste) {
                break;
            }
            pasteDialogShown = false;
            const ::std::string paste_source = state->getPendingPasteSource();
            const bool paste_is_cut = state->getPendingPasteIsCut();
            const ::std::string dst = state->getPendingPasteDst();
            const ::std::string entry_name = file::getLastPathSegment(dst);
            state->setPendingPasteSource("", false);
            state->setPendingAction(State::ActionNone);
            if (result == 0 && !paste_source.empty()) {
                // dst was last checked before the dialog was shown; a writer could
                // have replaced it while the user was looking at the confirmation.
                // Revalidate right before the destructive delete so we only ever
                // remove the exact file the user agreed to overwrite.
                struct stat current_stat {};
                const bool dst_unchanged = (stat(dst.c_str(), &current_stat) == 0) &&
                    state->pendingPasteDstMatches(current_stat);

                if (!dst_unchanged) {
                    LOG_W(TAG, "Overwrite: destination \"%s\" changed since confirmation, aborting", dst.c_str());
                    addPasteFailure(i18n::format(tr("\"{}\" changed while the dialog was open."), { entry_name }));
                } else if (file::deleteRecursively(dst)) {
                    // Trade-off: dst is removed before the copy attempt. If doPaste
                    // subsequently fails (e.g. source read error, out of space), the
                    // original dst data is unrecoverable. Acceptable for an embedded
                    // file manager; a safer approach would rename dst to a temp path
                    // first and roll back on failure.
                    doPaste(paste_source, paste_is_cut, dst);
                } else {
                    LOG_E(TAG, "Overwrite: failed to remove existing destination: \"%s\"", dst.c_str());
                    addPasteFailure(i18n::format(tr("Could not remove \"{}\" before overwriting."), { entry_name }));
                }
            }
            state->clearPendingPasteDstStat();
            processPasteQueue();
            break;
        }
        default:
            break;
    }
}

void View::onCopyPressed() {
    const auto paths = getSelectedPaths();
    state->setClipboard(paths, false);
    LOG_I(TAG, "Copied %u item(s) to clipboard", static_cast<unsigned>(paths.size()));
    onNavigate();
}

void View::onCutPressed() {
    const auto paths = getSelectedPaths();
    state->setClipboard(paths, true);
    LOG_I(TAG, "Cut %u item(s) to clipboard", static_cast<unsigned>(paths.size()));
    onNavigate();
}

void View::onPastePressed() {
    onNavigate();
    auto clipboard = state->getClipboard();
    if (!clipboard.has_value()) return;

    // The app task does the file system work, as it can block for long on slow storage
    state->setPasteRequest({ clipboard->first, state->getCurrentPath(), clipboard->second });
    task_event_group_signal(eventGroup, pasteBit);
}

void View::onPasteRequested() {
    auto request = state->takePasteRequest();
    if (!request.has_value()) return;

    // The pending destination, source and dialog belong to one paste: a second one waits for the answer
    if (pasteDialogShown || !pasteQueue.empty() || dialogOwner != DialogOwner::None) {
        LOG_W(TAG, "Paste: another paste or dialog is busy, skipping");
        return;
    }

    pasteQueue = State::toExt(request->sources);
    pasteFolder = State::toExt(request->destination_folder);
    pasteIsCut = request->is_cut;
    pasteFailures.clear();
    processPasteQueue();
}

void View::processPasteQueue() {
    while (!pasteQueue.empty()) {
        const ::std::string src(pasteQueue.front().data(), pasteQueue.front().size());
        pasteQueue.erase(pasteQueue.begin());

        const ::std::string entry_name = file::getLastPathSegment(src);
        const ::std::string dst = file::getChildPath(::std::string(pasteFolder.data(), pasteFolder.size()), entry_name);

        if (src == dst) {
            LOG_I(TAG, "Paste: source and destination are the same path, skipping");
            continue;
        }

        // Copying a folder into itself would never end, as the copy is listed while it is being made
        if (dst.starts_with(src + "/")) {
            LOG_W(TAG, "Paste: \"%s\" is inside \"%s\"", dst.c_str(), src.c_str());
            addPasteFailure(i18n::format(tr("\"{}\" can't be pasted into itself."), { entry_name }));
            continue;
        }

        struct stat dst_stat {};
        if (stat(dst.c_str(), &dst_stat) == 0) {
            state->setPendingPasteDst(dst);
            state->setPendingPasteSource(src, pasteIsCut);
            state->setPendingPasteDstStat(dst_stat);
            state->setPendingAction(State::ActionPaste);
            dialogOwner = DialogOwner::Paste;
            if (dialog::alert(appInstanceId, tr("File exists"), i18n::format(tr("Overwrite \"{}\"?"), { entry_name }), { tr("Overwrite"), tr("Cancel") }, onAlertClosed, this) == ERROR_NONE) {
                pasteDialogShown = true;
                return;
            }
            dialogOwner = DialogOwner::None;
            // The answer never comes, so the paste goes on with the other sources
            LOG_E(TAG, "Failed to start the overwrite confirmation for %s", dst.c_str());
            state->setPendingPasteSource("", false);
            state->clearPendingPasteDstStat();
            state->setPendingAction(State::ActionNone);
            addPasteFailure(i18n::format(tr("\"{}\" could not be copied."), { entry_name }));
            continue;
        }

        doPaste(src, pasteIsCut, dst);
    }

    reload();

    if (!pasteFailures.empty()) {
        ::std::string message;
        for (const auto& failure : pasteFailures) {
            if (!message.empty()) message += "\n";
            message.append(failure.data(), failure.size());
        }
        pasteFailures.clear();
        state->setPendingAction(State::ActionNone); // prevent re-trigger on dialog dismiss
        dialog::alert(appInstanceId, tr("Paste failed"), message);
    }
}

void View::doPaste(const ::std::string& src, bool is_cut, const ::std::string& dst) {
    OperationScope operation(*this);
    bool success = false;
    bool src_delete_failed = false;
    if (is_cut) {
        if (rename(src.c_str(), dst.c_str()) != 0) {
            LOG_W(TAG, "rename() of \"%s\" failed with errno %d", src.c_str(), errno);
            // Fallback for cross-filesystem moves: copy then delete.
            // Only mark success if both halves succeed. If the source removal
            // fails the clipboard is preserved and the error is surfaced.
            if (copyRecursive(src, dst)) {
                if (file::deleteRecursively(src)) {
                    success = true;
                } else {
                    src_delete_failed = true;
                    LOG_E(TAG, "Cut: copied \"%s\" to \"%s\" but failed to remove source", src.c_str(), dst.c_str());
                }
            }
        } else {
            success = true;
        }
    } else {
        success = copyRecursive(src, dst);
    }

    const ::std::string filename = file::getLastPathSegment(src);
    if (success) {
        LOG_I(TAG, "%s \"%s\" to \"%s\"", is_cut ? "Moved" : "Copied", src.c_str(), dst.c_str());
        if (is_cut) {
            state->removeFromClipboardIfCut(src);
        }
    } else if (src_delete_failed) {
        addPasteFailure(i18n::format(tr("\"{}\" was copied but the original could not be removed."), { filename }));
    } else {
        LOG_E(TAG, "Failed to %s \"%s\" to \"%s\" (errno %d)", is_cut ? "move" : "copy", src.c_str(), dst.c_str(), errno);
        addPasteFailure(i18n::format(is_cut ? tr("\"{}\" could not be moved.") : tr("\"{}\" could not be copied."), { filename }));
    }
}

} // namespace tt::app::files
