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
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/toolbar.h>

#include <generated_assets.h>

#include <Tactility/app/files/SupportedFiles.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/imageviewer/ImageViewer.h>
#include <Tactility/app/inputdialog/InputDialog.h>
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

static void onRenamePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onRenamePressed();
}

static void onDeletePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onDeletePressed();
}

static void onOverflowPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onOverflowPressed();
}

static void onNewFilePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNewFilePressed();
}

static void onNewFolderPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNewFolderPressed();
}

static void onCopyPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onCopyPressed();
}

static void onCutPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onCutPressed();
}

static void onEjectPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onEjectPressed();
}

static void onPastePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onPastePressed();
}

static void onRunPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onRunPressed();
}

static void onOverlayKeyCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onOverlayKey(lv_event_get_key(event));
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

struct NavigateJob {
    ::std::shared_ptr<State> state;
    ::std::string path;
    bool firstPage;
    bool selectFirst;
};

void navigateWork(void* context) {
    auto* job = static_cast<NavigateJob*>(context);
    if (job->state->setEntriesForPath(job->path)) {
        job->state->pushCompletedNavigation(job->firstPage, job->selectFirst);
    }
    delete job;
}

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
        alertdialog::start(job->appInstanceId, tr("Run failed"), i18n::format(tr("Could not run \"{}\"."), { file::getLastPathSegment(job->path) }));
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
    if (navigationPending.exchange(true)) {
        return;
    }
    auto* job = new (::std::nothrow) NavigateJob { state, path, firstPage, selectFirst };
    if (job == nullptr || file_ops_queue(navigateWork, job, &fileOpsSub) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue reading %s", path.c_str());
        delete job;
        navigationPending = false;
        return;
    }
    beginOperation();
}

void View::onFileOpsCompleted() {
    while (file_ops_poll(&fileOpsSub) == ERROR_NONE) {
        navigationPending = false;
        endOperation();
        auto result = state->takeCompletedNavigation();
        if (result.has_value()) {
            lvgl_lock();
            onNavigate();
            lvgl_unlock();
            update(result->first, result->second);
        }
    }
}

void View::viewFile(const ::std::string& path, const ::std::string& filename) {
    ::std::string file_path = path + "/" + filename;
    LOG_I(TAG, "Clicked %s", file_path.c_str());

    if (isSupportedAppFile(filename)) {
        // install(filename);
        auto message = i18n::format(tr("Do you want to install {}?"), { filename });
        installAppPath = file_path;
        auto choices = ::std::vector<::std::string> { tr("Yes"), tr("No") };
        installDialogId = alertdialog::start(appInstanceId, tr("Install?"), message, choices);
    } else if (isSupportedImageFile(filename)) {
        imageviewer::start(file_path);
    } else if (isSupportedTextFile(filename)) {
        notes::start(file_path);
    } else if (state->isExecutable(filename)) {
        runFile(file_path);
    } else {
        LOG_W(TAG, "Opening files of this type is not supported");
    }

    onNavigate();
}

void View::runFile(const ::std::string& file_path) {
    LOG_I(TAG, "Running %s", file_path.c_str());

    if (!state->isExecutable(file::getLastPathSegment(file_path))) {
        LOG_W(TAG, "Not executable: %s", file_path.c_str());
        alertdialog::start(appInstanceId, tr("Run failed"), i18n::format(tr("\"{}\" is not an executable."), { file::getLastPathSegment(file_path) }));
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
    eventGroup(eventGroup),
    grid(TileGrid::Callbacks {
        .collect = collectItems,
        .onClicked = onTileClicked,
        .onLongPressed = onTileLongPressed,
        .onKey = onTileKey,
        .resolveImage = resolveImage,
        .resolveIcon = resolveIcon,
        .onPageChanged = onPageChanged,
        .onTilesCleared = nullptr,
        .userData = this
    }) {
    check(task_event_group_claim_bit(eventGroup, &pasteBit) == ERROR_NONE);
    check(file_ops_subscribe(&fileOpsSub, eventGroup) == ERROR_NONE);
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
    file_ops_unsubscribe(&fileOpsSub);
    task_event_group_release_bit(eventGroup, pasteBit);
}

::std::vector<TileGridItem> View::collectItems(void* userData) {
    auto* view = static_cast<View*>(userData);
    const bool is_root = (view->state->getCurrentPath() == "/");
    ::std::vector<TileGridItem> items;
    view->state->withEntries([&](const ::std::vector<dirent>& entries) {
        items.reserve(entries.size());
        for (const auto& entry : entries) {
            const auto icon = getIcon(entry, is_root);
            const lv_image_dsc_t* image = icon == FileIcon::Unknown ? nullptr : reinterpret_cast<const lv_image_dsc_t*>(view->icons[static_cast<size_t>(icon)]);
            // Files that are resolved later are shown as an executable or a plain file, which are rasterized from the same set
            const bool recolor = view->iconRecolor[static_cast<size_t>(icon == FileIcon::Unknown ? FileIcon::File : icon)];
            // The glyph is shown when there is no image: without PSRAM or when it failed to rasterize
            const char* glyph = icon == FileIcon::Unknown ? nullptr : FILE_ICON_GLYPHS[static_cast<size_t>(icon)];
            items.push_back({ entry.d_name, entry.d_name, glyph, false, image, recolor });
        }
    });
    return items;
}

const char* View::resolveIcon(const TileGridItem& item, void* userData) {
    auto* view = static_cast<View*>(userData);
    return FILE_ICON_GLYPHS[static_cast<size_t>(view->state->isExecutable(item.id) ? FileIcon::Executable : FileIcon::File)];
}

const lv_image_dsc_t* View::resolveImage(const TileGridItem& item, void* userData) {
    auto* view = static_cast<View*>(userData);
    const auto icon = view->state->isExecutable(item.id) ? FileIcon::Executable : FileIcon::File;
    return reinterpret_cast<const lv_image_dsc_t*>(view->icons[static_cast<size_t>(icon)]);
}

void View::onTileClicked(const TileGridItem& item, void* userData) {
    static_cast<View*>(userData)->onDirEntryPressed(item.id);
}

void View::onTileLongPressed(const TileGridItem& item, void* userData) {
    static_cast<View*>(userData)->onDirEntryLongPressed(item.id);
}

void View::onTileKey(const TileGridItem& item, uint32_t key, void* userData) {
    static_cast<View*>(userData)->onDirEntryKeyPressed(item.id, key);
}

void View::onPageChanged(void* userData) {
    auto* view = static_cast<View*>(userData);
    const auto entries = view->state->getSelectedEntries();
    if (entries.empty()) {
        view->hideOverlay();
        return;
    }
    // The selection and its actions stay, and the tiles of the new page that are selected show it
    for (const auto& name : entries) {
        lv_obj_t* tile = view->grid.findTile(name);
        if (tile != nullptr) {
            lv_obj_add_state(tile, LV_STATE_CHECKED);
        }
    }
}

bool View::findDirent(const ::std::string& name, dirent& out_entry) {
    bool found = false;
    state->withEntries([&](const ::std::vector<dirent>& entries) {
        const auto it = ::std::ranges::find_if(entries, [&](const dirent& entry) { return name == entry.d_name; });
        if (it != entries.end()) {
            out_entry = *it;
            found = true;
        }
    });
    return found;
}

void View::onDirEntryPressed(const ::std::string& name) {
    // Once an entry is selected, a tap adds an entry to the selection or removes it
    if (isSelecting()) {
        dirent selected_entry;
        if (findDirent(name, selected_entry)) {
            toggleSelectedEntry(selected_entry);
        }
        return;
    }

    // A tap only closes the overlay, so it can't open an entry by accident
    if (!lv_obj_is_hidden(overlay)) {
        hideOverlay();
        return;
    }

    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }

    LOG_I(TAG, "Pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);
    state->setSelectedChildEntry(dir_entry.d_name);

    using namespace tt::file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR: {
            // A touch doesn't show a selection, so only keys select the first entry
            lv_obj_t* tile = grid.findTile(name);
            selectFirstAfterNavigation = tile != nullptr && lv_obj_has_state(tile, LV_STATE_FOCUS_KEY);
            navigateTo(file::getChildPath(state->getCurrentPath(), dir_entry.d_name), true, true);
            break;
        }

        case TT_DT_LNK:
            LOG_W(TAG, "opening links is not supported");
            break;

        default:
            viewFile(state->getCurrentPath(), dir_entry.d_name);
            onNavigate();
            break;
    }
}

bool View::isSelecting() const {
    // Actions are shown for a selection of entries. At the root only mount points have actions, so they can't be combined.
    return overlay != nullptr && !lv_obj_is_hidden(overlay) && state->getCurrentPath() != "/" && !state->getSelectedEntries().empty();
}

void View::toggleSelectedEntry(const dirent& dir_entry) {
    if (dir_entry.d_type == file::TT_DT_LNK) {
        return;
    }
    const ::std::string name = dir_entry.d_name;
    lv_obj_t* pressed_tile = grid.findTile(name);
    const bool selected = state->toggleSelectedEntry(name);
    if (pressed_tile != nullptr) {
        lv_obj_set_state(pressed_tile, LV_STATE_CHECKED, selected);
    }
    const auto entries = state->getSelectedEntries();
    if (entries.empty()) {
        hideOverlay();
    } else {
        state->setSelectedChildEntry(entries.front());
        showActionsForSelection(grid.findTile(entries.back()));
    }
}

void View::onDirEntryLongPressed(const ::std::string& name) {
    const bool is_root = state->getCurrentPath() == "/";
    const bool extends_selection = isSelecting();
    if (!extends_selection) {
        // Deselects the tile of an overlay that is still open
        hideOverlay();
    }

    dirent dir_entry;
    if (!findDirent(name, dir_entry)) {
        return;
    }

    LOG_I(TAG, "Long-pressed %s %d", dir_entry.d_name, (int)dir_entry.d_type);

    if (extends_selection) {
        toggleSelectedEntry(dir_entry);
        return;
    }

    state->setSelectedChildEntry(dir_entry.d_name);
    lv_obj_t* tile = grid.findTile(name);

    if (is_root) {
        // At root, only USB mount points support actions (eject).
        // Other root-level entries intentionally have no context actions.
        if (isUsbMountPoint(dir_entry.d_name)) {
            state->selectOnlyEntry(name);
            showActionsForMountPoint(tile);
        }
        return;
    }

    using namespace file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR:
            state->selectOnlyEntry(name);
            showActionsForDirectory(tile);
            break;

        case TT_DT_LNK:
            LOG_W(TAG, "Opening links is not supported");
            break;

        default:
            state->selectOnlyEntry(name);
            showActionsForFile(tile);
            break;
    }
}

void View::onDirEntryKeyPressed(const ::std::string& name, uint32_t key) {
    // Space selects like a long press. The focus stays on the grid, so more entries can be selected with space or enter.
    if (key == ' ') {
        keepGridFocus = true;
        onDirEntryLongPressed(name);
        keepGridFocus = false;
        return;
    }

    if (isSelecting()) {
        switch (key) {
            case LV_KEY_ESC:
                hideOverlay();
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
    hideOverlay();
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
    if (!lv_obj_is_hidden(overlay)) {
        hideOverlay();
        return;
    }

    lv_obj_clean(overlay);
    addOverlayButton(LVGL_ICON_SHARED_NOTE_ADD, onNewFilePressedCallback);
    addOverlayButton(LVGL_ICON_SHARED_CREATE_NEW_FOLDER, onNewFolderPressedCallback);
    if (state->hasClipboard()) {
        addOverlayButton(LVGL_ICON_SHARED_CONTENT_PASTE, onPastePressedCallback);
    }
    showOverlay(nullptr);
}

void View::onRenamePressed() {
    onNavigate();
    ::std::string entry_name = state->getSelectedChildEntry();
    LOG_I(TAG, "Pending rename %s", entry_name.c_str());
    state->setPendingAction(State::ActionRename);
    inputdialog::start(appInstanceId, tr("Rename"), "", entry_name, inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::onDeletePressed() {
    const auto paths = getSelectedPaths();
    onNavigate();
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
    const ::std::vector<::std::string> choices = { tr("Yes"), tr("No") };
    alertdialog::start(appInstanceId, tr("Are you sure?"), message, choices);
}

void View::onNewFilePressed() {
    onNavigate();
    LOG_I(TAG, "Creating new file");
    state->setPendingAction(State::ActionCreateFile);
    inputdialog::start(appInstanceId, tr("New File"), tr("Enter filename:"), "", inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

void View::onNewFolderPressed() {
    onNavigate();
    LOG_I(TAG, "Creating new folder");
    state->setPendingAction(State::ActionCreateFolder);
    inputdialog::start(appInstanceId, tr("New Folder"), tr("Enter folder name:"), "", inputDialogStream, inputDialogBuffer, sizeof(inputDialogBuffer), eventGroup);
}

lv_obj_t* View::addOverlayButton(const char* icon, lv_event_cb_t callback) {
    auto* button = lvgl_icon_button_create(overlay);
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, icon);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, this);
    return button;
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

void View::addCommonFileActions() {
    addOverlayButton(LVGL_ICON_SHARED_CONTENT_COPY, onCopyPressedCallback);
    addOverlayButton(LVGL_ICON_SHARED_CONTENT_CUT, onCutPressedCallback);
    if (state->hasClipboard()) {
        addOverlayButton(LVGL_ICON_SHARED_CONTENT_PASTE, onPastePressedCallback);
    }
    if (state->getSelectedEntries().size() <= 1) {
        addOverlayButton(LVGL_ICON_SHARED_EDIT, onRenamePressedCallback);
    }
    addOverlayButton(LVGL_ICON_SHARED_DELETE, onDeletePressedCallback);
}

// The overlay covers the bottom of the grid, or its top when it would cover the anchor tile
void View::showOverlay(lv_obj_t* anchorTile) {
    lv_obj_set_hidden(overlay, false);
    lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_update_layout(overlay);
    if (anchorTile != nullptr) {
        lv_obj_add_state(anchorTile, LV_STATE_CHECKED);
        lv_area_t tile_area;
        lv_area_t overlay_area;
        lv_obj_get_coords(anchorTile, &tile_area);
        lv_obj_get_coords(overlay, &overlay_area);
        if (tile_area.y2 > overlay_area.y1) {
            lv_obj_align_to(overlay, lv_obj_get_parent(anchorTile), LV_ALIGN_TOP_MID, 0, 0);
        }
    }

    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && !keepGridFocus && lv_obj_get_child_count(overlay) > 0) {
        lv_group_focus_obj(overlay);
        lv_gridnav_set_focused(overlay, lv_obj_get_child(overlay, 0), LV_ANIM_OFF);
        // Touch shows no selection, until a key moves the focus
        lv_indev_t* indev = lv_indev_active();
        if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lvgl_focus_hide_key_selection(group);
        }
    }
}

void View::hideOverlay() {
    if (overlay == nullptr || lv_obj_is_hidden(overlay)) {
        return;
    }
    lv_obj_set_hidden(overlay, true);

    for (const auto& name : state->getSelectedEntries()) {
        lv_obj_t* selected_tile = grid.findTile(name);
        if (selected_tile != nullptr) {
            lv_obj_remove_state(selected_tile, LV_STATE_CHECKED);
        }
    }
    state->clearSelectedEntries();

    lv_obj_t* tile = grid.findTile(state->getSelectedChildEntry());
    if (tile != nullptr) {
        lv_obj_remove_state(tile, LV_STATE_CHECKED);
    }

    // Keys continue on the tile that the overlay was for
    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && lv_group_get_focused(group) == overlay) {
        if (tile != nullptr) {
            lv_group_focus_obj(lv_obj_get_parent(tile));
            lv_gridnav_set_focused(lv_obj_get_parent(tile), tile, LV_ANIM_OFF);
        }
    }
}

void View::onOverlayKey(uint32_t key) {
    if (key == LV_KEY_ESC) {
        hideOverlay();
    }
}

void View::showActionsForDirectory(lv_obj_t* tile) {
    lv_obj_clean(overlay);
    addCommonFileActions();
    showOverlay(tile);
}

void View::showActionsForFile(lv_obj_t* tile) {
    lv_obj_clean(overlay);
    if (state->isExecutable(state->getSelectedChildEntry())) {
        addOverlayButton(LVGL_ICON_SHARED_PLAY_ARROW, onRunPressedCallback);
    }
    addCommonFileActions();
    showOverlay(tile);
}

void View::showActionsForSelection(lv_obj_t* tile) {
    const auto entries = state->getSelectedEntries();
    dirent dir_entry;
    if (entries.size() == 1 && findDirent(entries[0], dir_entry)) {
        if (dir_entry.d_type == file::TT_DT_DIR || dir_entry.d_type == file::TT_DT_CHR) {
            showActionsForDirectory(tile);
        } else {
            showActionsForFile(tile);
        }
        return;
    }
    lv_obj_clean(overlay);
    addCommonFileActions();
    showOverlay(tile);
}

void View::showActionsForMountPoint(lv_obj_t* tile) {
    lv_obj_clean(overlay);
    addOverlayButton(LVGL_ICON_SHARED_EJECT, onEjectPressedCallback);
    showOverlay(tile);
}

void View::onRunPressed() {
    ::std::string file_path = state->getSelectedChildPath();
    onNavigate();
    runFile(file_path);
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
        alertdialog::start(appInstanceId, tr("Eject failed"), i18n::format(tr("Could not eject \"{}\"."), { file::getLastPathSegment(mount_path) }));
    }

    onNavigate();
    navigateTo(state->getCurrentPath(), false, false);
}

lv_obj_t* View::getParentPathSegment() const {
    if (pathBarWidgets.size() >= 2 && lv_obj_is_clickable(pathBarWidgets[pathBarWidgets.size() - 2])) {
        return pathBarWidgets[pathBarWidgets.size() - 2];
    }
    return pathBarWidgets.back();
}

void View::onPathSegmentPressed(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (index < view->pathBarPaths.size()) {
        view->onNavigate();
        // Only when keys selected the segment, as a touch doesn't show a selection
        view->selectParentSegment = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_FOCUS_KEY);
        view->navigateTo(view->pathBarPaths[index], true, false);
    }
}

void View::onDeferredPathBarUpdate(void* userData) {
    auto* view = static_cast<View*>(userData);
    if (view->toolbar != nullptr) {
        view->updatePathBar();
    }
}

// The toolbar can change size when the system bars change. The path bar is rebuilt after the layout pass.
void View::onToolbarSizeChanged(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    if (lv_obj_get_width(view->toolbar) != view->pathBarWidth) {
        lv_async_call_cancel(onDeferredPathBarUpdate, view);
        lv_async_call(onDeferredPathBarUpdate, view);
    }
}

void View::updatePathBar() {
    for (auto* widget : pathBarWidgets) {
        lv_obj_delete(widget);
    }
    pathBarWidgets.clear();
    pathBarPaths.clear();

    // The root, and a path for every directory of the current path
    ::std::vector<::std::string> names = { "/" };
    pathBarPaths.push_back("/");
    const auto& current_path = state->getCurrentPath();
    ::std::string accumulated;
    size_t start = 0;
    while (start < current_path.size()) {
        if (current_path[start] == '/') {
            start++;
            continue;
        }
        size_t end = current_path.find('/', start);
        if (end == ::std::string::npos) {
            end = current_path.size();
        }
        names.push_back(current_path.substr(start, end - start));
        accumulated += "/" + names.back();
        pathBarPaths.push_back(accumulated);
        start = end;
    }

    lv_obj_update_layout(toolbar);

    // Shows the segments from the first one that is given, and an ellipsis in front of the first one when the path is cut off.
    // The current directory is checked and can't be pressed.
    auto build = [&](size_t first) {
        size_t index = 1;
        if (first > 0) {
            auto* ellipsis = lv_label_create(toolbar);
            lv_label_set_text(ellipsis, "..");
            lv_obj_move_to_index(ellipsis, static_cast<int32_t>(index++));
            pathBarWidgets.push_back(ellipsis);
        }
        for (size_t i = first; i < names.size(); i++) {
            auto* segment = lvgl_chip_create(toolbar);
            // The chip fits the height of the toolbar, with its focus ring inside, as the toolbar clips what is outside of it
            lv_obj_set_style_margin_ver(segment, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_pad_ver(segment, 0, LV_STATE_DEFAULT);
            lv_obj_set_height(segment, lv_obj_get_content_height(toolbar));
            lv_obj_set_flex_flow(segment, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(segment, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_add_state(segment, LV_STATE_FOCUS_KEY);
            const int32_t ring_width = lv_obj_get_style_outline_width(segment, LV_PART_MAIN);
            lv_obj_remove_state(segment, LV_STATE_FOCUS_KEY);
            lv_obj_set_style_outline_pad(segment, -ring_width, LV_STATE_FOCUS_KEY);
            if (lvgl_theme_is_mono()) {
                // The ring has the colour of the chip's border, so the selected chip is inverted instead
                lv_obj_set_style_bg_color(segment, lv_obj_get_style_text_color(segment, LV_PART_MAIN), LV_STATE_FOCUS_KEY);
                lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_STATE_FOCUS_KEY);
                lv_obj_set_style_text_color(segment, lv_obj_get_style_bg_color(segment, LV_PART_MAIN), LV_STATE_FOCUS_KEY);
            }
            lv_obj_t* label = lv_label_create(segment);
            lv_label_set_text(label, names[i].c_str());
            // One line with the width of the name. A content size would make the label wrap to the room that is left.
            lv_point_t text_size;
            lv_text_get_size(&text_size, names[i].c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_size(label, text_size.x, text_size.y);
            lv_obj_set_user_data(segment, reinterpret_cast<void*>(i));
            if (i + 1 == names.size()) {
                lv_obj_add_state(segment, LV_STATE_CHECKED);
                lv_obj_set_click_focusable(segment, false);
            } else {
                lv_obj_add_event_cb(segment, onPathSegmentPressed, LV_EVENT_SHORT_CLICKED, this);
            }
            lv_obj_move_to_index(segment, static_cast<int32_t>(index++));
            pathBarWidgets.push_back(segment);
        }
    };

    // How much wider the visible children of the toolbar are than the toolbar, with the gaps between them. 0 or less when they fit.
    auto overflow = [&] {
        lv_obj_update_layout(toolbar);
        int32_t used = 0;
        int32_t count = 0;
        const uint32_t child_count = lv_obj_get_child_count(toolbar);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* child = lv_obj_get_child(toolbar, static_cast<int32_t>(i));
            if (!lv_obj_is_hidden(child)) {
                // The margin of a chip, for its focus ring, takes room too
                used += lv_obj_get_width(child) + lv_obj_get_style_margin_left(child, LV_PART_MAIN) + lv_obj_get_style_margin_right(child, LV_PART_MAIN);
                count++;
            }
        }
        used += (count - 1) * lv_obj_get_style_pad_column(toolbar, LV_PART_MAIN);
        return used - lv_obj_get_content_width(toolbar);
    };

    size_t first = 0;
    build(first);
    // Drops the leftmost segments until the path fits, keeping at least the current directory and its parent
    while (overflow() > 0 && first + 2 < names.size()) {
        for (auto* widget : pathBarWidgets) {
            lv_obj_delete(widget);
        }
        pathBarWidgets.clear();
        first++;
        build(first);
    }
    // Shortens the name of the current directory when that is still not enough
    const int32_t excess = overflow();
    if (excess > 0) {
        lv_obj_t* label = lv_obj_get_child(pathBarWidgets.back(), 0);
        lv_obj_set_width(label, ::std::max<int32_t>(lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)), lv_obj_get_width(label) - excess));
    }
    pathBarWidth = lv_obj_get_width(toolbar);
    // The grid can still hold the chip that was deleted, as the fallback of a selection that waits for the repopulate
    grid.replaceEmptyFallback(getParentPathSegment());

    // The pressed segment is gone, so the selection moves to the segment below the new directory
    if (selectParentSegment) {
        selectParentSegment = false;
        lv_group_focus_obj(toolbar);
        lv_gridnav_set_focused(toolbar, getParentPathSegment(), LV_ANIM_OFF);
    }
}

void View::update(bool firstPage, bool selectFirst) {
    if (!lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "lvgl");
        return;
    }

    // The widgets are gone while another window covers this one
    if (toolbar == nullptr) {
        lvgl_unlock();
        return;
    }

    const bool is_root = (state->getCurrentPath() == "/");
    lv_obj_set_hidden(overflow_button, is_root);
    updatePathBar();

    if (firstPage) {
        grid.showFirstPage();
    }
    // Without a pointer device there is no touch, so a selection is always wanted
    selectFirst = selectFirst && (selectFirstAfterNavigation || !lvgl_indev_exists(LV_INDEV_TYPE_POINTER));
    selectFirstAfterNavigation = false;
    if (firstPage && selectFirst) {
        // The nearest segment of the path bar, when the directory has no entries
        grid.requestRepopulateSelectingFirst(getParentPathSegment());
    } else {
        grid.requestRepopulate(!firstPage);
    }

    lvgl_unlock();
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
    pathBarWidth = 0;
    updatePathBar();
    lv_obj_add_event_cb(toolbar, onToolbarSizeChanged, LV_EVENT_SIZE_CHANGED, this);

    grid.setSwipeNavigation(true);
    grid.setVerticalSwipeInSideLayout(true);
    grid.setSystemPageButtons(true);
    grid.setSideIndicator(true);
    grid.createWidgets(parent);

    // Floating, so showing it doesn't change the grid's size and page layout
    overlay = lvgl_card_create(parent);
    lv_obj_set_floating(overlay, true);
    lv_obj_set_size(overlay, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(overlay, false);
    lvgl_obj_add_edge_padding(overlay);
    // The arrow keys move between the overlay's buttons as one row
    lvgl_grid_navigation_add(overlay);
    lv_obj_add_event_cb(overlay, onOverlayKeyCallback, LV_EVENT_KEY, this);
    lv_obj_set_hidden(overlay, true);
}

void View::destroyWidgets() {
    // The checked tiles and the overlay are gone, so nothing may stay selected
    state->clearSelectedEntries();
    busy_spinner = nullptr;
    overlay = nullptr;
    lv_async_call_cancel(onDeferredPathBarUpdate, this);
    toolbar = nullptr;
    pathBarWidgets.clear();
    pathBarPaths.clear();
    overflow_button = nullptr;
    grid.destroyWidgets();
}

void View::onNavigate() {
    hideOverlay();
}

void View::onResult(uint32_t launchId, int32_t result) {
    if (launchId == installDialogId && result == 0) {
        app_install(installAppPath.c_str());
        return;
    }

    ::std::string filepath = state->getSelectedChildPath();
    LOG_I(TAG, "Result for %s", filepath.c_str());

    // Text-entry result (rename/new file/new folder), read from the AppStream bound to that
    // dialog's stdout. Empty for Cancel. Other pending actions (delete/paste confirmations) never
    // bound this stream; their switch cases below only look at `result`, not `resultText`.
    bool isTextEntryAction = state->getPendingAction() == State::ActionRename ||
        state->getPendingAction() == State::ActionCreateFile ||
        state->getPendingAction() == State::ActionCreateFolder;
    ::std::string resultText;
    if (isTextEntryAction) {
        if (result == 0) {
            char buffer[sizeof(inputDialogBuffer)];
            size_t length = app_stream_read(&inputDialogStream, buffer, sizeof(buffer));
            resultText = ::std::string(buffer, length);
        }
        app_stream_unsubscribe(&inputDialogStream);
    }

    // The answer of the overwrite confirmation counts even when the pending action was reset meanwhile
    const auto action = (pasteDialogId != 0 && launchId == pasteDialogId) ? State::ActionPaste : state->getPendingAction();
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

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
            }
            break;
        }
        case State::ActionRename: {
            ::std::string new_name = resultText;
            if (!new_name.empty() && new_name != state->getSelectedChildEntry()) {
                ::std::string rename_to = file::getChildPath(state->getCurrentPath(), new_name);
                struct stat st;
                if (stat(rename_to.c_str(), &st) == 0) {
                    LOG_W(TAG, "Rename: destination already exists: \"%s\"", rename_to.c_str());
                    state->setPendingAction(State::ActionNone);
                    alertdialog::start(appInstanceId, tr("Rename failed"), i18n::format(tr("\"{}\" already exists."), { new_name }));
                    break;
                }
                if (rename(filepath.c_str(), rename_to.c_str()) == 0) {
                    LOG_I(TAG, "Renamed \"%s\" to \"%s\"", filepath.c_str(), rename_to.c_str());
                } else {
                    LOG_E(TAG, "Failed to rename \"%s\" to \"%s\"", filepath.c_str(), rename_to.c_str());
                }

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
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

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
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

                state->setEntriesForPath(state->getCurrentPath());
                update(false);
            }
            break;
        }
        case State::ActionPaste: {
            // Only the answer of the confirmation that belongs to the pending paste counts
            if (launchId != pasteDialogId) {
                break;
            }
            pasteDialogId = 0;
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
    update(false);
}

void View::onCutPressed() {
    const auto paths = getSelectedPaths();
    state->setClipboard(paths, true);
    LOG_I(TAG, "Cut %u item(s) to clipboard", static_cast<unsigned>(paths.size()));
    onNavigate();
    update(false);
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
    if (pasteDialogId != 0 || !pasteQueue.empty()) {
        LOG_W(TAG, "Paste: another paste is busy, skipping");
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
            const ::std::vector<::std::string> choices = { tr("Overwrite"), tr("Cancel") };
            pasteDialogId = alertdialog::start(appInstanceId, tr("File exists"), i18n::format(tr("Overwrite \"{}\"?"), { entry_name }), choices);
            if (pasteDialogId != 0) {
                return;
            }
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

    state->setEntriesForPath(state->getCurrentPath());
    update(false);

    if (!pasteFailures.empty()) {
        ::std::string message;
        for (const auto& failure : pasteFailures) {
            if (!message.empty()) message += "\n";
            message.append(failure.data(), failure.size());
        }
        pasteFailures.clear();
        state->setPendingAction(State::ActionNone); // prevent re-trigger on dialog dismiss
        alertdialog::start(appInstanceId, tr("Paste failed"), message);
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
