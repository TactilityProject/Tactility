// SPDX-License-Identifier: Apache-2.0
#include "sdl_display.h"
#include "sdl_bridge.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <SDL2/SDL.h>

#include <cstdlib>

constexpr auto* TAG = "SdlDisplay";
#define GET_CONFIG(device) (static_cast<const SdlDisplayConfig*>((device)->config))

struct SdlDisplayInternal {
    bool initialized;
    bool init_failed;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
};

// Only one sdl-display device exists in the simulator; sdl_input.cpp uses this to map window
// coordinates back to the fixed logical resolution SDL_RenderSetLogicalSize() scales to the window.
static SdlDisplayInternal* g_display_internal = nullptr;

SDL_Renderer* sdl_display_get_renderer(void) {
    return g_display_internal != nullptr ? g_display_internal->renderer : nullptr;
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
    auto* internal = static_cast<SdlDisplayInternal*>(malloc(sizeof(SdlDisplayInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->initialized = false;
    internal->init_failed = false;
    internal->window = nullptr;
    internal->renderer = nullptr;
    internal->texture = nullptr;

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

    free(internal);
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
    const int initial_width = static_cast<int>(config->horizontal_resolution * dpi_scale);
    const int initial_height = static_cast<int>(config->vertical_resolution * dpi_scale);

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
        SDL_RenderSetLogicalSize(internal->renderer, config->horizontal_resolution, config->vertical_resolution);
    }
    internal->texture = internal->renderer != nullptr
        ? SDL_CreateTexture(internal->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
            config->horizontal_resolution, config->vertical_resolution)
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

    SDL_Rect rect = { x_start, y_start, x_end - x_start, y_end - y_start };
    // RGB565 = 2 bytes/pixel.
    if (SDL_UpdateTexture(internal->texture, &rect, color_data, (x_end - x_start) * 2) != 0) {
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

static enum DisplayColorFormat sdl_display_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t sdl_display_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t sdl_display_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

// endregion

static const DisplayApi sdl_display_api = {
    .capabilities = 0,
    .reset = sdl_display_reset,
    .init = sdl_display_init,
    .draw_bitmap = sdl_display_draw_bitmap,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
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
};

extern Module simulator_module;

Driver sdl_display_driver = {
    .name = "sdl-display",
    .compatible = (const char*[]) { "tactility,sdl-display", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &sdl_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &simulator_module,
    .internal = nullptr
};
