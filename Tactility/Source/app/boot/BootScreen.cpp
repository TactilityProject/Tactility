#include <Tactility/app/boot/BootScreen.h>

#include <Tactility/settings/DisplaySettings.h>

#include <font/fonts.h>
#include <graphics/pixel_buffer.h>
#include <lodepng/lodepng.h>

#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/pointer.h>
#include <tactility/log.h>

#include <algorithm>
#include <memory>

namespace tt::app::boot {

constexpr auto* TAG = "BootScreen";
constexpr int BAND_HEIGHT = 16;
constexpr uint16_t TEXT_COLOR = 0xFFFF;

namespace {

struct Logo {
    unsigned char* rgba = nullptr;
    unsigned width = 0;
    unsigned height = 0;

    ~Logo() { lodepng_free(rgba); }
};

/** Splits text into lines of at most maxCharacters, breaking at spaces where possible */
void wrapLine(const std::string& text, size_t maxCharacters, std::vector<std::string>& output) {
    size_t start = 0;
    while (text.size() - start > maxCharacters) {
        size_t end = text.rfind(' ', start + maxCharacters);
        if (end == std::string::npos || end <= start) {
            end = start + maxCharacters;
            output.push_back(text.substr(start, end - start));
            start = end;
        } else {
            output.push_back(text.substr(start, end - start));
            start = end + 1;
        }
    }
    output.push_back(text.substr(start));
}

uint16_t toRgb565(uint32_t red, uint32_t green, uint32_t blue) {
    return static_cast<uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

bool collectReadyDevice(Device* device, void* context) {
    if (device_is_ready(device) && device_get(device) == ERROR_NONE) {
        static_cast<std::vector<Device*>*>(context)->push_back(device);
    }
    return true;
}

std::vector<Device*> getReadyKeyboards() {
    std::vector<Device*> keyboards;
    device_for_each_of_type(&KEYBOARD_TYPE, &keyboards, collectReadyDevice);
    return keyboards;
}

} // namespace

BootScreen::~BootScreen() {
    end();
}

bool BootScreen::begin() {
    if (device_get_first_by_type(&DISPLAY_TYPE, &display) != ERROR_NONE) {
        LOG_I(TAG, "No display");
        display = nullptr;
        return false;
    }

    panelWidth = display_get_resolution_x(display);
    panelHeight = display_get_resolution_y(display);
    const auto orientation = settings::display::loadOrGetDefault().orientation;
    rotation = static_cast<int>(settings::display::toLvglDisplayRotation(orientation));

    const auto color_format = display_get_color_format(display);
    if (display_get_frame_buffer_count(display) > 0) {
        // Drawing into the hardware frame buffer avoids a copy, and a partial update to a
        // double-buffered panel would flip to a half-drawn frame
        void* frame_buffers[2] = { nullptr, nullptr };
        display_get_frame_buffer(display, 0, &frame_buffers[0]);
        if (display_get_frame_buffer_count(display) > 1) {
            display_get_frame_buffer(display, 1, &frame_buffers[1]);
        }
        if (frame_buffers[0] != nullptr) {
            target = pixel_buffer_wrap(frame_buffers[0], color_format, panelWidth, panelHeight, 0);
        }
        if (target != nullptr && frame_buffers[1] != nullptr) {
            secondFrameBuffer = pixel_buffer_wrap(frame_buffers[1], color_format, panelWidth, panelHeight, 0);
        }
        bandHeight = panelHeight;
    }

    if (target == nullptr) {
        const bool full_frame = color_format == DISPLAY_COLOR_FORMAT_MONOCHROME || display_has_capability(display, DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME);
        bandHeight = full_frame ? panelHeight : std::min(BAND_HEIGHT, panelHeight);
        target = pixel_buffer_create(color_format, panelWidth, bandHeight);
    }

    if (target == nullptr) {
        LOG_E(TAG, "Failed to allocate buffer");
        end();
        return false;
    }

    return true;
}

void BootScreen::end() {
    pixel_buffer_free(target);
    pixel_buffer_free(secondFrameBuffer);
    target = nullptr;
    secondFrameBuffer = nullptr;
    if (display != nullptr) {
        device_put(display);
        display = nullptr;
    }
}

int BootScreen::getSmallestDimension() const {
    return std::min(panelWidth, panelHeight);
}

void BootScreen::show(const std::string& logoPath, const std::vector<std::string>& lines) {
    if (target == nullptr) {
        return;
    }

    Logo logo;
    if (!logoPath.empty()) {
        const unsigned error = lodepng_decode32_file(&logo.rgba, &logo.width, &logo.height, logoPath.c_str());
        if (error != 0) {
            LOG_E(TAG, "Failed to load %s: %s", logoPath.c_str(), lodepng_error_text(error));
            lodepng_free(logo.rgba);
            logo.rgba = nullptr;
            logo.width = 0;
            logo.height = 0;
        }
    }

    // Layout in logical (rotated) coordinates: logo with text below it, centered as a whole
    const auto& font = TT_TERMINAL_FONT_SYMBOL;
    const int logical_width = logicalWidth();
    const int logical_height = logicalHeight();
    const size_t max_characters = std::max(1, logical_width / font.glyph_width - 2);
    std::vector<std::string> wrapped_lines;
    for (const auto& line : lines) {
        wrapLine(line, max_characters, wrapped_lines);
    }
    const int text_height = static_cast<int>(wrapped_lines.size()) * font.glyph_height;
    const int gap = (logo.height > 0 && !wrapped_lines.empty()) ? font.glyph_height : 0;
    const int content_top = (logical_height - static_cast<int>(logo.height) - gap - text_height) / 2;
    const int logo_left = (logical_width - static_cast<int>(logo.width)) / 2;
    const int text_top = content_top + static_cast<int>(logo.height) + gap;

    for (int band_top = 0; band_top < panelHeight; band_top += bandHeight) {
        const int band_rows = std::min(bandHeight, panelHeight - band_top);
        pixel_buffer_clear(target);

        // Maps logical coordinates to the physical panel, like lv_display_rotate_area()
        auto plot = [&](int x, int y, uint16_t color, PixelBufferConversion conversion) {
            int physical_x;
            int physical_y;
            switch (rotation) {
                case 1: physical_x = y; physical_y = panelHeight - 1 - x; break;
                case 2: physical_x = panelWidth - 1 - x; physical_y = panelHeight - 1 - y; break;
                case 3: physical_x = panelWidth - 1 - y; physical_y = x; break;
                default: physical_x = x; physical_y = y; break;
            }
            if (physical_y >= band_top && physical_y < band_top + band_rows) {
                pixel_buffer_set_pixel_rgb565(target, physical_x, physical_y - band_top, color, conversion);
            }
        };

        for (unsigned y = 0; y < logo.height; y++) {
            for (unsigned x = 0; x < logo.width; x++) {
                const unsigned char* pixel = logo.rgba + (static_cast<size_t>(y) * logo.width + x) * 4;
                const uint32_t alpha = pixel[3];
                if (alpha != 0) {
                    // Blended onto the black background
                    const uint16_t color = toRgb565(pixel[0] * alpha / 255, pixel[1] * alpha / 255, pixel[2] * alpha / 255);
                    plot(logo_left + static_cast<int>(x), content_top + static_cast<int>(y), color, PIXEL_BUFFER_CONVERSION_LUMA_THRESHOLD);
                }
            }
        }

        for (size_t line_index = 0; line_index < wrapped_lines.size(); line_index++) {
            const auto& line = wrapped_lines[line_index];
            const int line_left = (logical_width - static_cast<int>(line.size()) * font.glyph_width) / 2;
            const int line_top = text_top + static_cast<int>(line_index) * font.glyph_height;
            for (size_t character_index = 0; character_index < line.size(); character_index++) {
                const auto character = static_cast<unsigned char>(line[character_index]);
                if (character < font.first_codepoint || character > font.last_codepoint) {
                    continue;
                }
                const uint8_t* glyph = &font.glyph_bitmap[(character - font.first_codepoint) * font.glyph_height * font.glyph_bytes_per_row];
                const int glyph_left = line_left + static_cast<int>(character_index) * font.glyph_width;
                for (int row = 0; row < font.glyph_height; row++) {
                    const uint8_t* bits = &glyph[row * font.glyph_bytes_per_row];
                    for (int column = 0; column < font.glyph_width; column++) {
                        if (bits[column / 8] & (0x80U >> (column % 8))) {
                            plot(glyph_left + column, line_top + row, TEXT_COLOR, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
                        }
                    }
                }
            }
        }

        pixel_buffer_msync(target, 0, 0, panelWidth, band_rows);
        display_draw_bitmap(display, 0, band_top, panelWidth, band_top + band_rows, pixel_buffer_get_data(target));
    }

    if (secondFrameBuffer != nullptr) {
        pixel_buffer_blit(secondFrameBuffer, 0, 0, target, 0, 0, panelWidth, panelHeight, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
        pixel_buffer_msync(secondFrameBuffer, 0, 0, panelWidth, panelHeight);
    }
}

std::string getInputPrompt(const std::string& action) {
    auto keyboards = getReadyKeyboards();
    const bool has_keyboard = !keyboards.empty();
    for (auto* keyboard : keyboards) {
        device_put(keyboard);
    }
    if (has_keyboard) {
        return "Press any key to " + action;
    }

    Device* pointer = nullptr;
    if (device_get_first_active_by_type(&POINTER_TYPE, &pointer) == ERROR_NONE) {
        device_put(pointer);
        return "Touch the screen to " + action;
    }

    return "";
}

void waitForInput() {
    auto keyboards = getReadyKeyboards();
    // Subscriptions must stay at the same address while subscribed
    auto subscriptions = std::make_unique<KeyboardEventSubscription[]>(keyboards.size());
    for (size_t i = 0; i < keyboards.size(); i++) {
        keyboard_subscribe(keyboards[i], &subscriptions[i]);
    }

    Device* pointer = nullptr;
    if (device_get_first_active_by_type(&POINTER_TYPE, &pointer) != ERROR_NONE) {
        pointer = nullptr;
    }

    // A touch only counts when it starts after the screen was seen untouched
    bool pointer_released = false;
    bool done = false;
    while (!done) {
        for (size_t i = 0; i < keyboards.size() && !done; i++) {
            // Drivers that implement read_key only deliver events to subscribers when polled
            KeyboardKeyData data;
            while (keyboard_read_key(keyboards[i], &data) == ERROR_NONE && data.key != 0 && data.continue_reading) {}

            while (keyboard_poll(keyboards[i], &subscriptions[i], &data) == ERROR_NONE) {
                if (data.pressed) {
                    done = true;
                }
            }
        }

        if (pointer != nullptr && !done && pointer_read_data(pointer, 0) == ERROR_NONE) {
            uint16_t x, y, strength;
            uint8_t point_count = 0;
            const bool touched = pointer_get_touched_points(pointer, &x, &y, &strength, &point_count, 1) && point_count > 0;
            if (!touched) {
                pointer_released = true;
            } else if (pointer_released) {
                done = true;
            }
        }

        if (!done) {
            delay_millis(20);
        }
    }

    for (size_t i = 0; i < keyboards.size(); i++) {
        keyboard_unsubscribe(keyboards[i], &subscriptions[i]);
        device_put(keyboards[i]);
    }
    if (pointer != nullptr) {
        device_put(pointer);
    }
}

}
