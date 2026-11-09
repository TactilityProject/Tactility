// SPDX-License-Identifier: Apache-2.0
#include "sdl_display.h"
#include "sdl_bridge.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <SDL2/SDL.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

constexpr auto* TAG = "SdlDisplay";
#define GET_CONFIG(device) (static_cast<const SdlDisplayConfig*>((device)->config))

struct SdlDisplayInternal {
    bool initialized = false;
    bool init_failed = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    // The window shows the oriented view: with swap_xy its width and height are swapped.
    // The requested state is set by the LVGL task when it rotates the display. The shown state is
    // only touched by the real main thread, which applies the request before it draws.
    std::atomic<bool> requested_swap_xy { false };
    std::atomic<bool> requested_mirror_x { false };
    std::atomic<bool> requested_mirror_y { false };
    bool shown_swap_xy = false;
    const SdlDisplayConfig* config = nullptr;
};

// Only one sdl-display device exists in the simulator; sdl_input.cpp uses this to map window
// coordinates back to the fixed logical resolution SDL_RenderSetLogicalSize() scales to the window.
static SdlDisplayInternal* g_display_internal = nullptr;

// The size of the oriented view that the window shows
static void sdl_display_compute_view_size(const SdlDisplayConfig* config, bool swap_xy, int* width, int* height) {
    *width = swap_xy ? config->vertical_resolution : config->horizontal_resolution;
    *height = swap_xy ? config->horizontal_resolution : config->vertical_resolution;
}

SDL_Renderer* sdl_display_get_renderer(void) {
    return g_display_internal != nullptr ? g_display_internal->renderer : nullptr;
}

bool sdl_display_get_view_size(int* width, int* height) {
    SdlDisplayInternal* internal = g_display_internal;
    if (internal == nullptr || internal->config == nullptr) {
        return false;
    }
    sdl_display_compute_view_size(internal->config, internal->requested_swap_xy.load(), width, height);
    return true;
}

// LVGL turns a native position (px, py) into a position in the rotated view (lx, ly), with W and H
// the native size: 90 degrees is (H - 1 - py, px), 180 is (W - 1 - px, H - 1 - py) and 270 is
// (py, W - 1 - px). This is the inverse. The rotation follows from the axes that LVGL asked the
// display to swap and mirror for it (see lvgl_display_apply_rotation()).
void sdl_display_view_to_native(int32_t* x, int32_t* y) {
    SdlDisplayInternal* internal = g_display_internal;
    if (internal == nullptr || internal->config == nullptr) {
        return;
    }
    const int32_t width = internal->config->horizontal_resolution;
    const int32_t height = internal->config->vertical_resolution;
    const int32_t view_x = *x;
    const int32_t view_y = *y;
    if (internal->requested_swap_xy.load()) {
        if (internal->requested_mirror_x.load()) {
            // 270 degrees
            *x = width - 1 - view_y;
            *y = view_x;
        } else {
            // 90 degrees
            *x = view_y;
            *y = height - 1 - view_x;
        }
    } else if (internal->requested_mirror_x.load() && internal->requested_mirror_y.load()) {
        *x = width - 1 - view_x;
        *y = height - 1 - view_y;
    }
}

// Re-blits the already-drawn texture at the renderer's current (possibly just-resized) scale.
// No new pixel data needed: the window resizing doesn't change what LVGL last rendered, only how
// large it should appear, and SDL only applies that until the next SDL_RenderPresent() call.
void sdl_display_present_now(void) {
    if (g_display_internal == nullptr) {
        return;
    }
    SDL_RenderClear(g_display_internal->renderer);
    SDL_RenderCopy(g_display_internal->renderer, g_display_internal->texture, nullptr, nullptr);
    SDL_RenderPresent(g_display_internal->renderer);
}

// region Driver lifecycle

static error_t start(Device* device) {
    auto* internal = new(std::nothrow) SdlDisplayInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->config = GET_CONFIG(device);
    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<SdlDisplayInternal*>(device_get_driver_data(device));

    if (internal->initialized) {
        SDL_DestroyTexture(internal->texture);
        SDL_DestroyRenderer(internal->renderer);
        SDL_DestroyWindow(internal->window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    if (g_display_internal == internal) {
        g_display_internal = nullptr;
    }

    delete internal;
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t sdl_display_reset(Device*) { return ERROR_NONE; }
static error_t sdl_display_init(Device*) { return ERROR_NONE; }

static float sdl_display_get_dpi_scale() {
    float hdpi = 96.0f;
    if (SDL_GetDisplayDPI(0, nullptr, &hdpi, nullptr) != 0 || hdpi <= 0.0f) {
        return 1.0f;
    }
    return hdpi / 96.0f;
}

static bool sdl_display_lazy_init(Device* device, SdlDisplayInternal* internal) {
    const auto* config = GET_CONFIG(device);

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        LOG_E(TAG, "SDL_InitSubSystem failed: %s", SDL_GetError());
        return false;
    }

    // Only the window's initial on-screen footprint scales here - the render/logical resolution
    // (config->horizontal_resolution/vertical_resolution, used below for the texture and
    // SDL_RenderSetLogicalSize()) is unaffected, same as any other resize the user does by hand.
    const float dpi_scale = sdl_display_get_dpi_scale();
    internal->shown_swap_xy = internal->requested_swap_xy.load();
    int view_width, view_height;
    sdl_display_compute_view_size(config, internal->shown_swap_xy, &view_width, &view_height);
    const int initial_width = static_cast<int>(view_width * dpi_scale);
    const int initial_height = static_cast<int>(view_height * dpi_scale);

    internal->window = SDL_CreateWindow(
        "Tactility",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        initial_width, initial_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    internal->renderer = internal->window != nullptr
        ? SDL_CreateRenderer(internal->window, -1, SDL_RENDERER_ACCELERATED)
        : nullptr;
    // Lets the window be resized freely while the renderer scales/letterboxes the fixed-resolution
    // texture below to fit - LVGL keeps rendering at horizontal_resolution x vertical_resolution.
    if (internal->renderer != nullptr) {
        SDL_RenderSetLogicalSize(internal->renderer, view_width, view_height);
    }
    internal->texture = internal->renderer != nullptr
        ? SDL_CreateTexture(internal->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, view_width, view_height)
        : nullptr;

    if (internal->window == nullptr || internal->renderer == nullptr || internal->texture == nullptr) {
        LOG_E(TAG, "Failed to create SDL window: %s", SDL_GetError());
        if (internal->texture != nullptr) SDL_DestroyTexture(internal->texture);
        if (internal->renderer != nullptr) SDL_DestroyRenderer(internal->renderer);
        if (internal->window != nullptr) SDL_DestroyWindow(internal->window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    g_display_internal = internal;
    return true;
}

// Swaps the width and height of the view when swap_xy changed: the window turns from landscape to
// portrait or back, and keeps its zoom. Only called on the real main thread. Mirroring needs no
// pixel work: the window shows the view as the user sees it, which is already upright.
static bool sdl_display_apply_orientation(const SdlDisplayConfig* config, SdlDisplayInternal* internal) {
    const bool swap_xy = internal->requested_swap_xy.load();
    if (swap_xy == internal->shown_swap_xy) {
        return true;
    }

    int view_width, view_height;
    sdl_display_compute_view_size(config, swap_xy, &view_width, &view_height);
    SDL_Texture* texture = SDL_CreateTexture(internal->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, view_width, view_height);
    if (texture == nullptr) {
        LOG_E(TAG, "Failed to create the rotated texture: %s", SDL_GetError());
        return false;
    }
    SDL_DestroyTexture(internal->texture);
    internal->texture = texture;
    internal->shown_swap_xy = swap_xy;

    int window_width, window_height;
    SDL_GetWindowSize(internal->window, &window_width, &window_height);
    SDL_RenderSetLogicalSize(internal->renderer, view_width, view_height);
    // Triggers SDL_WINDOWEVENT_SIZE_CHANGED, which presents the (empty) view until LVGL redraws it
    SDL_SetWindowSize(internal->window, window_height, window_width);
    return true;
}

// Only ever called from sdl_bridge_run_main_loop() on the real main thread - required for
// SDL/Cocoa window creation and rendering on macOS.
error_t sdl_display_execute_draw_bitmap(Device* device, void* internal_ptr, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<SdlDisplayInternal*>(internal_ptr);

    if (internal->init_failed) {
        return ERROR_RESOURCE;
    }

    if (!internal->initialized) {
        if (!sdl_display_lazy_init(device, internal)) {
            internal->init_failed = true;
            return ERROR_RESOURCE;
        }
        internal->initialized = true;
    }

    if (!sdl_display_apply_orientation(GET_CONFIG(device), internal)) {
        return ERROR_RESOURCE;
    }

    SDL_Rect rect = { x_start, y_start, x_end - x_start, y_end - y_start };
    // The texture is always RGB565 = 2 bytes/pixel.
    std::vector<uint16_t> converted;
    if (GET_CONFIG(device)->grayscale) {
        const auto* luminance = static_cast<const uint8_t*>(color_data);
        converted.resize(static_cast<size_t>(rect.w) * rect.h);
        for (size_t i = 0; i < converted.size(); i++) {
            const uint16_t value = luminance[i];
            converted[i] = ((value >> 3) << 11) | ((value >> 2) << 5) | (value >> 3);
        }
        color_data = converted.data();
    }
    if (SDL_UpdateTexture(internal->texture, &rect, color_data, rect.w * 2) != 0) {
        return ERROR_RESOURCE;
    }

    sdl_display_present_now();
    return ERROR_NONE;
}

static error_t sdl_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<SdlDisplayInternal*>(device_get_driver_data(device));

    if (internal->init_failed) {
        return ERROR_RESOURCE;
    }

    return sdl_bridge_present(device, internal, x_start, y_start, x_end, y_end, color_data);
}

static error_t sdl_display_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<SdlDisplayInternal*>(device_get_driver_data(device));
    internal->requested_mirror_x.store(x_axis);
    internal->requested_mirror_y.store(y_axis);
    return ERROR_NONE;
}

static error_t sdl_display_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<SdlDisplayInternal*>(device_get_driver_data(device));
    internal->requested_swap_xy.store(swap_axes);
    return ERROR_NONE;
}

static bool sdl_display_get_swap_xy(Device* device) {
    return static_cast<SdlDisplayInternal*>(device_get_driver_data(device))->requested_swap_xy.load();
}

static bool sdl_display_get_mirror_x(Device* device) {
    return static_cast<SdlDisplayInternal*>(device_get_driver_data(device))->requested_mirror_x.load();
}

static bool sdl_display_get_mirror_y(Device* device) {
    return static_cast<SdlDisplayInternal*>(device_get_driver_data(device))->requested_mirror_y.load();
}

static enum DisplayColorFormat sdl_display_get_color_format(Device* device) {
    return GET_CONFIG(device)->grayscale ? DISPLAY_COLOR_FORMAT_GRAYSCALE8 : DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t sdl_display_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t sdl_display_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

// endregion

static const DisplayApi sdl_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY,
    .reset = sdl_display_reset,
    .init = sdl_display_init,
    .draw_bitmap = sdl_display_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = sdl_display_mirror,
    .swap_xy = sdl_display_swap_xy,
    .get_swap_xy = sdl_display_get_swap_xy,
    .get_mirror_x = sdl_display_get_mirror_x,
    .get_mirror_y = sdl_display_get_mirror_y,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = nullptr,
    .disp_sleep = nullptr,
    .get_color_format = sdl_display_get_color_format,
    .get_resolution_x = sdl_display_get_resolution_x,
    .get_resolution_y = sdl_display_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

extern Module simulator_module;

Driver sdl_display_driver = {
    .name = "sdl-display",
    .compatible = (const char*[]) { "tactility,sdl-display", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &sdl_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr,
};
