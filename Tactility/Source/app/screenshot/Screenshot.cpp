#include <Tactility/TactilityConfig.h>

#if TT_FEATURE_SCREENSHOT_ENABLED

#include <Tactility/lvgl/Lvgl.h>

#include <app/manifest.h>

#include <tactility/filesystem/fs.h>
#include <tactility/freertos/freertos.h>
#include <tactility/paths.h>

#include <lvgl/lvgl.h>

#include <lv_screenshot.h>

#include <cstdio>
#include <string>

namespace tt::app::screenshot {

namespace {

constexpr TickType_t LVGL_LOCK_TIMEOUT = pdMS_TO_TICKS(1000);

/** The default path is in the temp folder, so "tt screenshot" can pull and remove it */
bool getDefaultPath(char* out_path, size_t out_size) {
    char temp_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_temp_path(temp_path, sizeof(temp_path)) != ERROR_NONE) {
        return false;
    }
    if (!directory_exists(temp_path)) {
        const error_t error = directory_make(temp_path, true);
        if (error != ERROR_NONE && error != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    const int written = snprintf(out_path, out_size, "%s/screenshot.png", temp_path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

int32_t main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && argv[1][0] != '/')) {
        printf("usage: screenshot [absolute path of the .png file]\n");
        return 1;
    }

    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (argc == 2) {
        snprintf(path, sizeof(path), "%s", argv[1]);
    } else if (!getDefaultPath(path, sizeof(path))) {
        printf("screenshot: no storage available\n");
        return 1;
    }

    if (!lvgl_try_lock(LVGL_LOCK_TIMEOUT)) {
        printf("screenshot: the display is busy\n");
        return 1;
    }
    // lv_screenshot writes through lv_fs, which requires the drive prefix
    const std::string lvgl_path = std::string(lvgl::PATH_PREFIX) + path;
    const bool success = lv_screenshot_create(lv_screen_active(), LV_100ASK_SCREENSHOT_SV_PNG, lvgl_path.c_str());
    lvgl_unlock();

    if (!success) {
        printf("screenshot: failed to save %s\n", path);
        return 1;
    }
    printf("%s\n", path);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "screenshot",
    .name = "screenshot",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HEADLESS,
    // Stack size is LVGL stack size (9 kB) plus 1 kB.
    .stack = { .depth = 10240, .desired_memory_capability = 0 },
};

} // namespace tt::app::screenshot

#endif
