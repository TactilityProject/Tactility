// SPDX-License-Identifier: Apache-2.0
#include "sdl_display.h"

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
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<SdlDisplayInternal*>(malloc(sizeof(SdlDisplayInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        LOG_E(TAG, "SDL_InitSubSystem failed: %s", SDL_GetError());
        free(internal);
        return ERROR_RESOURCE;
    }

    internal->window = SDL_CreateWindow(
        "Tactility",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        config->horizontal_resolution, config->vertical_resolution,
        SDL_WINDOW_SHOWN
    );
    internal->renderer = internal->window != nullptr
        ? SDL_CreateRenderer(internal->window, -1, SDL_RENDERER_ACCELERATED)
        : nullptr;
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
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<SdlDisplayInternal*>(device_get_driver_data(device));

    SDL_DestroyTexture(internal->texture);
    SDL_DestroyRenderer(internal->renderer);
    SDL_DestroyWindow(internal->window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t sdl_display_reset(Device*) { return ERROR_NONE; }
static error_t sdl_display_init(Device*) { return ERROR_NONE; }

static error_t sdl_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<SdlDisplayInternal*>(device_get_driver_data(device));

    SDL_Rect rect = { x_start, y_start, x_end - x_start, y_end - y_start };
    // RGB565 = 2 bytes/pixel.
    if (SDL_UpdateTexture(internal->texture, &rect, color_data, (x_end - x_start) * 2) != 0) {
        return ERROR_RESOURCE;
    }

    SDL_RenderClear(internal->renderer);
    SDL_RenderCopy(internal->renderer, internal->texture, nullptr, nullptr);
    SDL_RenderPresent(internal->renderer);
    return ERROR_NONE;
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

static void sdl_display_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t sdl_display_get_frame_buffer_count(Device*) {
    return 0;
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
    .get_frame_buffer = sdl_display_get_frame_buffer,
    .get_frame_buffer_count = sdl_display_get_frame_buffer_count,
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
