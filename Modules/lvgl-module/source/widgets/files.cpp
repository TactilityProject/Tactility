// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration and lv_obj_t's layout

#include <lvgl/widgets/files.h>

#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>

#include <tactility/Memory.hpp>
#include <tactility/file_ops.h>
#include <tactility/log.h>
#include <tactility/std/vector.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

constexpr auto* TAG = "lvgl_files";

namespace {

// The entry types, as reported in dirent.d_type
constexpr unsigned char TYPE_CHARACTER_DEVICE = 2;
constexpr unsigned char TYPE_DIRECTORY = 4;
constexpr unsigned char TYPE_LINK = 10;

struct Entry {
    dirent entry;
    uint32_t kind;
};

struct Files;

/** A directory read. Owned by the file operations task until it is done, and then by the LVGL task. */
struct LoadJob {
    /** The view that wants the result, or nullptr when it doesn't anymore. Guarded by the LVGL lock. */
    Files* files;
    std::string path;
    bool firstPage;
    bool selectFirst;
    /** Whether the result is ready and the apply is scheduled. Guarded by the LVGL lock. */
    bool done = false;
    LvglFilesCallbacks callbacks;
    tt::std::vector<Entry> entries;
    error_t error = ERROR_NONE;
};

struct Files {
    lv_obj_t* obj = nullptr;
    lv_obj_t* grid = nullptr;
    LvglFilesCallbacks callbacks = {};
    std::string path;
    tt::std::vector<Entry> entries;
    /** The read that the view waits for */
    LoadJob* pendingJob = nullptr;
};

struct FilesObject {
    lv_obj_t obj;
    Files* files;
};

// Not themed: the view is an invisible container of the tile grid
const lv_obj_class_t lvgl_files_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "files",
    .width_def = LV_PCT(100),
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(FilesObject),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

Files* getFiles(lv_obj_t* obj) {
    if (obj == nullptr || !lv_obj_check_type(obj, &lvgl_files_class)) {
        return nullptr;
    }
    return reinterpret_cast<FilesObject*>(obj)->files;
}

bool isDirectory(const dirent& entry) {
    return entry.d_type == TYPE_DIRECTORY || entry.d_type == TYPE_CHARACTER_DEVICE;
}

const Entry* findEntry(const Files* self, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(self->entries, [name](const Entry& entry) { return strcmp(entry.entry.d_name, name) == 0; });
    return it != self->entries.end() ? &*it : nullptr;
}

void applyJob(void* context);

// Runs on the file operations task
void readWork(void* context) {
    auto* job = static_cast<LoadJob*>(context);
    DIR* dir = opendir(job->path.c_str());
    if (dir == nullptr) {
        LOG_E(TAG, "Failed to open %s", job->path.c_str());
        job->error = ERROR_NOT_FOUND;
    } else {
        dirent* entry;
        // readdir() returns null at the end and on failure, and only a failure sets errno
        while ((errno = 0, entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (job->callbacks.filter != nullptr && !job->callbacks.filter(job->path.c_str(), entry, job->callbacks.user_data)) {
                continue;
            }
            const uint32_t kind = job->callbacks.classify != nullptr ? job->callbacks.classify(job->path.c_str(), entry, job->callbacks.user_data) : 0;
            job->entries.push_back({ *entry, kind });
        }
        if (errno != 0) {
            LOG_E(TAG, "Failed to read %s", job->path.c_str());
            job->error = ERROR_RESOURCE;
        }
        closedir(dir);
        // Directories first, then by name
        std::ranges::sort(job->entries, [](const Entry& left, const Entry& right) {
            const bool leftIsDirectory = isDirectory(left.entry);
            const bool rightIsDirectory = isDirectory(right.entry);
            if (leftIsDirectory != rightIsDirectory) {
                return leftIsDirectory;
            }
            return strcmp(left.entry.d_name, right.entry.d_name) < 0;
        });
    }

    lvgl_lock();
    if (job->files == nullptr) {
        // The view was deleted, or a newer read replaced this one
        tt::deleteObject(job);
    } else {
        job->done = true;
        if (lv_async_call(applyJob, job) != LV_RESULT_OK) {
            // Reported from here, as the read has to end
            LOG_E(TAG, "Failed to schedule showing %s", job->path.c_str());
            job->error = ERROR_OUT_OF_MEMORY;
            applyJob(job);
        }
    }
    lvgl_unlock();
}

/** Lets go of a read that the view doesn't want anymore. With the LVGL lock held. */
void abandonJob(LoadJob* job) {
    if (job == nullptr) {
        return;
    }
    if (job->done) {
        lv_async_call_cancel(applyJob, job);
        tt::deleteObject(job);
    } else {
        // The file operations task deletes it when it is done
        job->files = nullptr;
    }
}

// Runs on the LVGL task
void applyJob(void* context) {
    auto* job = static_cast<LoadJob*>(context);
    Files* self = job->files;
    if (self == nullptr) {
        tt::deleteObject(job);
        return;
    }
    self->pendingJob = nullptr;
    lv_obj_t* obj = self->obj;
    if (job->error == ERROR_NONE) {
        self->path = job->path;
        self->entries = std::move(job->entries);
        if (job->firstPage) {
            lvgl_tile_grid_show_first_page(self->grid);
        }
        if (job->selectFirst) {
            lvgl_tile_grid_request_repopulate_selecting_first(self->grid, nullptr);
        } else {
            lvgl_tile_grid_request_repopulate(self->grid, !job->firstPage);
        }
    }
    const std::string path = job->path;
    const error_t error = job->error;
    tt::deleteObject(job);
    if (self->callbacks.on_loaded != nullptr) {
        self->callbacks.on_loaded(obj, path.c_str(), error, self->callbacks.user_data);
    }
}

void onDeleted(lv_event_t* e) {
    auto* self = static_cast<Files*>(lv_event_get_user_data(e));
    abandonJob(self->pendingJob);
    reinterpret_cast<FilesObject*>(self->obj)->files = nullptr;
    tt::deleteObject(self);
}

// region Tile grid callbacks

Files* getFilesOfGrid(void* userData) {
    return static_cast<Files*>(userData);
}

void collect(lv_obj_t* grid, void* userData) {
    Files* self = getFilesOfGrid(userData);
    for (const auto& entry : self->entries) {
        LvglFilesIcon icon = {};
        if (isDirectory(entry.entry)) {
            icon.glyph = LVGL_ICON_SHARED_FOLDER;
        } else if (entry.entry.d_type == TYPE_LINK) {
            icon.glyph = LVGL_ICON_SHARED_LINK;
        } else {
            icon.glyph = LVGL_ICON_SHARED_DRAFT;
        }
        if (self->callbacks.get_icon != nullptr) {
            self->callbacks.get_icon(self->obj, &entry.entry, entry.kind, &icon, self->callbacks.user_data);
        }
        const LvglTileGridItem item = {
            .id = entry.entry.d_name,
            .name = entry.entry.d_name,
            .icon = icon.glyph,
            .highlighted = false,
            .image = icon.image,
            .image_recolor = icon.image_recolor,
            .user_data = nullptr,
            .checked = self->callbacks.is_checked != nullptr && self->callbacks.is_checked(self->obj, &entry.entry, entry.kind, self->callbacks.user_data),
        };
        lvgl_tile_grid_add_item(grid, &item);
    }
}

void onTileClicked(lv_obj_t*, const LvglTileGridItem* item, void* userData) {
    Files* self = getFilesOfGrid(userData);
    const Entry* entry = findEntry(self, item->id);
    if (entry != nullptr && self->callbacks.on_clicked != nullptr) {
        // A copy, as the callback can show another directory
        const Entry copy = *entry;
        self->callbacks.on_clicked(self->obj, &copy.entry, copy.kind, self->callbacks.user_data);
    }
}

void onTileLongPressed(lv_obj_t*, const LvglTileGridItem* item, void* userData) {
    Files* self = getFilesOfGrid(userData);
    const Entry* entry = findEntry(self, item->id);
    if (entry != nullptr && self->callbacks.on_long_pressed != nullptr) {
        const Entry copy = *entry;
        self->callbacks.on_long_pressed(self->obj, &copy.entry, copy.kind, self->callbacks.user_data);
    }
}

void onTileKey(lv_obj_t*, const LvglTileGridItem* item, uint32_t key, void* userData) {
    Files* self = getFilesOfGrid(userData);
    const Entry* entry = findEntry(self, item->id);
    if (entry != nullptr && self->callbacks.on_key != nullptr) {
        const Entry copy = *entry;
        self->callbacks.on_key(self->obj, &copy.entry, copy.kind, key, self->callbacks.user_data);
    }
}

void onPageChanged(lv_obj_t*, void* userData) {
    Files* self = getFilesOfGrid(userData);
    if (self->callbacks.on_page_changed != nullptr) {
        self->callbacks.on_page_changed(self->obj, self->callbacks.user_data);
    }
}

// endregion

error_t queueRead(Files* self, const std::string& path, bool firstPage, bool selectFirst) {
    auto* job = tt::newObject<LoadJob>(self, path, firstPage, selectFirst);
    if (job == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    job->callbacks = self->callbacks;
    // Holds the LVGL lock, so the job can't complete before it is the pending one
    lvgl_lock();
    if (file_ops_queue(readWork, job, nullptr) != ERROR_NONE) {
        lvgl_unlock();
        LOG_E(TAG, "Failed to queue reading %s", path.c_str());
        tt::deleteObject(job);
        return ERROR_OUT_OF_MEMORY;
    }
    abandonJob(self->pendingJob);
    self->pendingJob = job;
    lvgl_unlock();
    return ERROR_NONE;
}

} // namespace

extern "C" {

lv_obj_t* lvgl_files_create(lv_obj_t* parent, const LvglFilesConfig* config) {
    auto* self = tt::newObject<Files>();
    if (self == nullptr) {
        return nullptr;
    }
    if (config != nullptr) {
        self->callbacks = config->callbacks;
    }

    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_files_class, parent);
    lv_obj_class_init_obj(obj);
    reinterpret_cast<FilesObject*>(obj)->files = self;
    self->obj = obj;
    lv_obj_set_flex_grow(obj, 1);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(obj, false);
    lv_obj_add_event_cb(obj, onDeleted, LV_EVENT_DELETE, self);

    const LvglTileGridConfig gridConfig = {
        .style = LVGL_TILE_GRID_STYLE_TILE,
        .icon_color = LVGL_TILE_GRID_ICON_COLOR_DEFAULT,
        .swipe_navigation = config != nullptr && config->swipe_navigation,
        .callbacks = {
            .collect = collect,
            .on_clicked = onTileClicked,
            .on_long_pressed = onTileLongPressed,
            .on_key = onTileKey,
            .resolve_image = nullptr,
            .resolve_icon = nullptr,
            .on_page_changed = onPageChanged,
            .on_tiles_cleared = nullptr,
            .user_data = self,
        },
        .paging = config != nullptr ? config->paging : nullptr,
        .page = config != nullptr ? config->page : 0,
    };
    self->grid = lvgl_tile_grid_create(obj, &gridConfig);
    if (self->grid == nullptr) {
        // Frees the state through the delete event
        lv_obj_delete(obj);
        return nullptr;
    }
    return obj;
}

error_t lvgl_files_set_path(lv_obj_t* files, const char* path, bool first_page, bool select_first) {
    Files* self = getFiles(files);
    if (self == nullptr || path == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    return queueRead(self, path, first_page, select_first);
}

error_t lvgl_files_reload(lv_obj_t* files) {
    Files* self = getFiles(files);
    if (self == nullptr || self->path.empty()) {
        return ERROR_INVALID_ARGUMENT;
    }
    return queueRead(self, self->path, false, false);
}

const char* lvgl_files_get_path(lv_obj_t* files) {
    Files* self = getFiles(files);
    return self != nullptr ? self->path.c_str() : "";
}

bool lvgl_files_is_loading(lv_obj_t* files) {
    Files* self = getFiles(files);
    return self != nullptr && self->pendingJob != nullptr;
}

bool lvgl_files_find_entry(lv_obj_t* files, const char* name, struct dirent* entry, uint32_t* kind) {
    Files* self = getFiles(files);
    const Entry* found = self != nullptr ? findEntry(self, name) : nullptr;
    if (found == nullptr) {
        return false;
    }
    if (entry != nullptr) {
        *entry = found->entry;
    }
    if (kind != nullptr) {
        *kind = found->kind;
    }
    return true;
}

uint32_t lvgl_files_get_entry_count(lv_obj_t* files) {
    Files* self = getFiles(files);
    return self != nullptr ? static_cast<uint32_t>(self->entries.size()) : 0;
}

lv_obj_t* lvgl_files_find_tile(lv_obj_t* files, const char* name) {
    Files* self = getFiles(files);
    return self != nullptr ? lvgl_tile_grid_find_tile(self->grid, name) : nullptr;
}

lv_obj_t* lvgl_files_get_tile_grid(lv_obj_t* files) {
    Files* self = getFiles(files);
    return self != nullptr ? self->grid : nullptr;
}

bool lvgl_files_entry_is_directory(const struct dirent* entry) {
    return entry != nullptr && isDirectory(*entry);
}

bool lvgl_files_entry_is_link(const struct dirent* entry) {
    return entry != nullptr && entry->d_type == TYPE_LINK;
}

} // extern "C"
