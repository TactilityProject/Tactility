#include <Tactility/app/boot/BootScreen.h>

#include <Tactility/settings/DisplaySettings.h>

#include <Tactility/lvgl/Fonts.h>

#include <binfont/binfont.h>
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
    if (textFont != nullptr) {
        binfont_close(textFont);
        textFont = nullptr;
    }
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

    draw(static_cast<int>(logo.width), static_cast<int>(logo.height), [&logo](int x, int y, uint16_t& color) {
        const unsigned char* pixel = logo.rgba + (static_cast<size_t>(y) * logo.width + x) * 4;
        const uint32_t alpha = pixel[3];
        if (alpha == 0) {
            return false;
        }
        // Blended onto the black background
        color = toRgb565(pixel[0] * alpha / 255, pixel[1] * alpha / 255, pixel[2] * alpha / 255);
        return true;
    }, lines);
}

void BootScreen::showQrCode(int moduleCount, const std::function<bool(int x, int y)>& isModuleSet, const std::vector<std::string>& lines) {
    if (target == nullptr || moduleCount <= 0) {
        return;
    }

    // About 60% of the display, which leaves room for the text
    const int module_size = std::max(1, getSmallestDimension() * 6 / 10 / moduleCount);
    const int size = module_size * moduleCount;
    draw(size, size, [&isModuleSet, module_size](int x, int y, uint16_t& color) {
        if (!isModuleSet(x / module_size, y / module_size)) {
            return false;
        }
        color = 0xFFFF;
        return true;
    }, lines);
}

void BootScreen::draw(int imageWidth, int imageHeight, const ImagePixel& imagePixel, const std::vector<std::string>& lines) {
    if (target == nullptr) {
        return;
    }

    // Layout in logical (rotated) coordinates: the image with text below it, centered as a whole
    if (!lines.empty() && textFont == nullptr) {
        textFont = lvgl::loadMonoFont();
        if (textFont == nullptr) {
            LOG_E(TAG, "Failed to load font, text is not shown");
        }
    }
    BinFontMetrics metrics = {};
    BinFontGlyph reference = {};
    const bool has_font = textFont != nullptr && binfont_get_glyph(textFont, 'M', &reference);
    if (has_font) {
        binfont_get_metrics(textFont, &metrics);
    }
    const int character_width = has_font ? std::max(1, static_cast<int>((reference.advance_x16 + 8) >> 4)) : 1;
    const int line_height = has_font ? metrics.line_height : 0;
    const int logical_width = logicalWidth();
    const int logical_height = logicalHeight();
    std::vector<std::string> wrapped_lines;
    if (has_font) {
        const size_t max_characters = std::max(1, logical_width / character_width - 2);
        for (const auto& line : lines) {
            wrapLine(line, max_characters, wrapped_lines);
        }
    }
    const int text_height = static_cast<int>(wrapped_lines.size()) * line_height;
    const int gap = (imageHeight > 0 && !wrapped_lines.empty()) ? line_height : 0;
    const int content_top = (logical_height - imageHeight - gap - text_height) / 2;
    const int image_left = (logical_width - imageWidth) / 2;
    const int text_top = content_top + imageHeight + gap;

    std::vector<uint8_t> glyph_bitmap;
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

        for (int y = 0; y < imageHeight; y++) {
            for (int x = 0; x < imageWidth; x++) {
                uint16_t color;
                if (imagePixel(x, y, color)) {
                    plot(image_left + x, content_top + y, color, PIXEL_BUFFER_CONVERSION_LUMA_THRESHOLD);
                }
            }
        }

        for (size_t line_index = 0; line_index < wrapped_lines.size(); line_index++) {
            const auto& line = wrapped_lines[line_index];
            const int line_left = (logical_width - static_cast<int>(line.size()) * character_width) / 2;
            const int baseline = text_top + static_cast<int>(line_index) * line_height + metrics.ascent;
            for (size_t character_index = 0; character_index < line.size(); character_index++) {
                BinFontGlyph glyph;
                if (!binfont_get_glyph(textFont, static_cast<unsigned char>(line[character_index]), &glyph) || glyph.width == 0 || glyph.height == 0) {
                    continue;
                }
                glyph_bitmap.resize(static_cast<size_t>(glyph.width) * glyph.height);
                if (binfont_get_glyph_bitmap(textFont, &glyph, glyph_bitmap.data(), glyph.width) != ERROR_NONE) {
                    continue;
                }
                const int glyph_left = line_left + static_cast<int>(character_index) * character_width + glyph.x;
                const int glyph_top = baseline - glyph.y - glyph.height;
                for (int row = 0; row < glyph.height; row++) {
                    for (int column = 0; column < glyph.width; column++) {
                        const uint8_t alpha = glyph_bitmap[row * glyph.width + column];
                        if (alpha != 0) {
                            // White blended onto the black background
                            plot(glyph_left + column, glyph_top + row, toRgb565(alpha, alpha, alpha), PIXEL_BUFFER_CONVERSION_LUMA_THRESHOLD);
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
