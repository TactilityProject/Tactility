#include "save_png.h"

#include <lodepng/lodepng.h>
#include <lvgl.h>

bool lv_screenshot_save_png_file(const uint8_t* image, uint32_t w, uint32_t h, uint32_t bpp, const char* filename) {
    unsigned char* png = NULL;
    size_t png_size = 0;
    unsigned error;
    if (bpp == 32) {
        error = lodepng_encode32(&png, &png_size, image, w, h);
    } else if (bpp == 24) {
        error = lodepng_encode24(&png, &png_size, image, w, h);
    } else {
        return false;
    }
    if (error) {
        lodepng_free(png);
        return false;
    }

    // Written through lv_fs, so LVGL drive letter paths ("A:...") work
    bool success = false;
    lv_fs_file_t file;
    if (lv_fs_open(&file, filename, LV_FS_MODE_WR) == LV_FS_RES_OK) {
        uint32_t written = 0;
        success = lv_fs_write(&file, png, png_size, &written) == LV_FS_RES_OK && written == png_size;
        lv_fs_close(&file);
    }
    lodepng_free(png);
    return success;
}
