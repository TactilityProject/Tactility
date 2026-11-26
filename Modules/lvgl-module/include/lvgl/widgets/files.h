// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>
#include <lvgl/widgets/tile_grid.h>

#include <tactility/error.h>

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The icon of an entry: an image, or a glyph of the shared icon font when there is no image */
typedef struct {
    /** A glyph of the shared icon font (LVGL_ICON_SHARED_*) */
    const char* glyph;
    /** Shown instead of the glyph when set. Must outlive the widget. */
    const lv_image_dsc_t* image;
    /** Paints the image in the icon colour, for single colour images */
    bool image_recolor;
} LvglFilesIcon;

/** What the files view asks of and reports to its owner. All functions are optional. */
typedef struct {
    /**
     * Runs on the file operations task for every entry while a directory is read, so it may access files (e.g. to tell
     * whether a file is an executable). It must not use LVGL.
     * @return a value that is kept with the entry and passed to the other callbacks, e.g. a file type. 0 without this callback.
     */
    uint32_t (*classify)(const char* directory, const struct dirent* entry, void* user_data);
    /**
     * Runs on the file operations task for every entry while a directory is read, before classify. It must not use LVGL.
     * @return false to leave the entry out. Without this callback, every entry is shown.
     */
    bool (*filter)(const char* directory, const struct dirent* entry, void* user_data);
    /** Sets the icon of an entry. Without this callback, directories have a folder, links a link and files a document. */
    void (*get_icon)(lv_obj_t* files, const struct dirent* entry, uint32_t kind, LvglFilesIcon* icon, void* user_data);
    /** Whether the tile of an entry is shown checked (LV_STATE_CHECKED) when it is built, e.g. because it is selected */
    bool (*is_checked)(lv_obj_t* files, const struct dirent* entry, uint32_t kind, void* user_data);
    /** An entry was clicked (not swiped) */
    void (*on_clicked)(lv_obj_t* files, const struct dirent* entry, uint32_t kind, void* user_data);
    void (*on_long_pressed)(lv_obj_t* files, const struct dirent* entry, uint32_t kind, void* user_data);
    /** A key was pressed on an entry */
    void (*on_key)(lv_obj_t* files, const struct dirent* entry, uint32_t kind, uint32_t key, void* user_data);
    /** The user went to another page */
    void (*on_page_changed)(lv_obj_t* files, void* user_data);
    /**
     * A directory was read and its entries are shown from the next rebuild, which is already requested, or reading it failed.
     * The path stays the one before when it failed.
     * Normally called on the LVGL task. When that can't be scheduled, it is called on the file operations task with the LVGL lock held.
     * @param[in] path the directory that was read
     * @param[in] error ERROR_NONE, ERROR_NOT_FOUND when the directory could not be opened, ERROR_RESOURCE when reading it failed,
     * or ERROR_OUT_OF_MEMORY when it could not be shown
     */
    void (*on_loaded)(lv_obj_t* files, const char* path, error_t error, void* user_data);
    /**
     * Passed to the callbacks. A directory read that is pending when the view is deleted still calls classify and filter,
     * so what they use must stay valid until then: work that is queued with file_ops_queue() after it runs when it completed.
     */
    void* user_data;
} LvglFilesCallbacks;

typedef struct {
    /** Copied */
    LvglFilesCallbacks callbacks;
    /** Swiping over the entries goes to the next or previous page */
    bool swipe_navigation;
    /** Adapts the paging to its surroundings, see LvglTileGridPaging. May be NULL. Must outlive the widget. */
    const LvglTileGridPaging* paging;
    /** The page that is shown first, see LvglTileGridConfig::page */
    uint32_t page;
} LvglFilesConfig;

/**
 * @brief Creates a files view: the entries of a directory as a paged grid of tiles, directories first and by name, with page dots
 * that are hidden when there is only one page. The arrow keys move between the entries.
 * The view is as wide as its parent and grows in a flex column (flex grow 1). It is empty until a path is set.
 * @param[in] parent the parent object, normally with a flex column layout
 * @param[in] config the configuration, which is copied
 * @return the created view, or NULL when out of memory
 */
lv_obj_t* lvgl_files_create(lv_obj_t* parent, const LvglFilesConfig* config);

/**
 * @brief Reads a directory on the file operations task, and shows it when it was read. The current entries stay until then.
 * A newer call replaces a read that is still pending. Reports the result to on_loaded.
 * @param[in] files the view
 * @param[in] path the directory, which is copied
 * @param[in] first_page show the first page, instead of the page of the selected entry. Without select_first, no entry stays selected.
 * @param[in] select_first select the first entry, like lvgl_tile_grid_request_repopulate_selecting_first(). Otherwise the selection stays, unless first_page is set.
 * @retval ERROR_NONE the read is queued
 * @retval ERROR_OUT_OF_MEMORY the read could not be queued
 */
error_t lvgl_files_set_path(lv_obj_t* files, const char* path, bool first_page, bool select_first);

/** @brief Reads the shown directory again, and keeps the page and the selection. */
error_t lvgl_files_reload(lv_obj_t* files);

/** @return the directory that is shown, or "" before one was read. Valid until the next directory is shown. */
const char* lvgl_files_get_path(lv_obj_t* files);

/** @return whether a directory is being read */
bool lvgl_files_is_loading(lv_obj_t* files);

/**
 * @brief Finds an entry of the shown directory.
 * @param[in] files the view
 * @param[in] name the name of the entry
 * @param[out] entry the entry, may be NULL
 * @param[out] kind the value of classify, may be NULL
 * @return whether the entry exists
 */
bool lvgl_files_find_entry(lv_obj_t* files, const char* name, struct dirent* entry, uint32_t* kind);

/** @return the number of entries of the shown directory */
uint32_t lvgl_files_get_entry_count(lv_obj_t* files);

/** @return the tile of the entry on the shown page, or NULL */
lv_obj_t* lvgl_files_find_tile(lv_obj_t* files, const char* name);

/** @return the tile grid of the entries, e.g. for lvgl_tile_grid_show_first_page() or lvgl_tile_grid_next_page() */
lv_obj_t* lvgl_files_get_tile_grid(lv_obj_t* files);

/** @return whether the entry is a directory, or a mount point (a character device) */
bool lvgl_files_entry_is_directory(const struct dirent* entry);

/** @return whether the entry is a symbolic link */
bool lvgl_files_entry_is_link(const struct dirent* entry);

#ifdef __cplusplus
}
#endif
