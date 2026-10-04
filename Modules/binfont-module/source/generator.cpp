// SPDX-License-Identifier: Apache-2.0
#include <binfont/generator.h>

#include <tactility/memory.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(size, user) ((void)(user), memory_alloc(size))
#define STBTT_free(pointer, user) ((void)(user), memory_free(pointer))
#include "stb_truetype.h"

namespace {

constexpr size_t HEAD_SIZE = 48;
constexpr uint8_t CMAP_FORMAT_0_TINY = 2;
constexpr uint8_t CMAP_FORMAT_SPARSE_TINY = 3;
constexpr uint32_t CMAP_MAX_RANGE = 0xFFFF;

constexpr MemoryPolicy EXTERNAL_PREFERRED = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

class ByteBuffer {
    uint8_t* data = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    bool failed = false;

    bool reserve(size_t required) {
        if (failed) {
            return false;
        }
        if (required <= capacity) {
            return true;
        }
        size_t new_capacity = capacity ? capacity * 2 : 4096;
        while (new_capacity < required) {
            new_capacity *= 2;
        }
        auto* new_data = static_cast<uint8_t*>(memory_realloc_with_policy(data, new_capacity, &EXTERNAL_PREFERRED));
        if (new_data == nullptr) {
            failed = true;
            return false;
        }
        data = new_data;
        capacity = new_capacity;
        return true;
    }

public:
    ~ByteBuffer() { memory_free(data); }

    size_t getSize() const { return size; }
    bool hasFailed() const { return failed; }

    void append(const void* bytes, size_t count) {
        if (reserve(size + count)) {
            memcpy(data + size, bytes, count);
            size += count;
        }
    }

    void appendZeros(size_t count) {
        if (reserve(size + count)) {
            memset(data + size, 0, count);
            size += count;
        }
    }

    void appendU8(uint8_t value) { append(&value, 1); }

    void appendU16(uint16_t value) {
        const uint8_t bytes[2] = { static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8) };
        append(bytes, 2);
    }

    void appendU32(uint32_t value) {
        const uint8_t bytes[4] = { static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24) };
        append(bytes, 4);
    }

    void setU32(size_t offset, uint32_t value) {
        if (!failed) {
            data[offset] = static_cast<uint8_t>(value);
            data[offset + 1] = static_cast<uint8_t>(value >> 8);
            data[offset + 2] = static_cast<uint8_t>(value >> 16);
            data[offset + 3] = static_cast<uint8_t>(value >> 24);
        }
    }

    void alignTo4() { appendZeros((4 - size % 4) % 4); }

    /** Starts a record and returns its offset, for endRecord(). */
    size_t beginRecord(const char* marker) {
        const size_t offset = size;
        appendU32(0);
        append(marker, 4);
        return offset;
    }

    void endRecord(size_t offset) { setU32(offset, static_cast<uint32_t>(size - offset)); }

    uint8_t* release() {
        uint8_t* result = data;
        data = nullptr;
        size = 0;
        capacity = 0;
        return result;
    }
};

/** MSB-first bit writer */
class BitWriter {
    ByteBuffer& buffer;
    uint8_t current = 0;
    uint8_t used = 0;

public:
    explicit BitWriter(ByteBuffer& buffer) : buffer(buffer) {}

    void write(uint32_t value, uint8_t bits) {
        while (bits > 0) {
            bits--;
            current = static_cast<uint8_t>((current << 1) | ((value >> bits) & 1U));
            used++;
            if (used == 8) {
                buffer.appendU8(current);
                current = 0;
                used = 0;
            }
        }
    }

    void flush() {
        if (used > 0) {
            buffer.appendU8(static_cast<uint8_t>(current << (8 - used)));
            current = 0;
            used = 0;
        }
    }
};

struct GlyphInfo {
    uint32_t codepoint;
    int ttfIndex;
    uint32_t advanceX16;
    int x0, y0, x1, y1; // stb_truetype bitmap box: y points down
};

uint8_t bits_for_unsigned(uint32_t value) {
    uint8_t bits = 1;
    while (bits < 32 && (value >> bits) != 0) {
        bits++;
    }
    return bits;
}

uint8_t bits_for_signed(int32_t min, int32_t max) {
    uint8_t bits = 1;
    while (bits < 32 && (min < -(1 << (bits - 1)) || max > (1 << (bits - 1)) - 1)) {
        bits++;
    }
    return bits;
}

int16_t scale_metric(int value, float scale) {
    return static_cast<int16_t>(std::lround(static_cast<float>(value) * scale));
}

uint8_t* read_file(const char* path, size_t& size) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return nullptr;
    }
    uint8_t* data = nullptr;
    long file_size = 0;
    if (fseek(file, 0, SEEK_END) == 0 && (file_size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0) {
        data = static_cast<uint8_t*>(memory_alloc_with_policy(static_cast<size_t>(file_size), &EXTERNAL_PREFERRED));
        if (data != nullptr && fread(data, 1, static_cast<size_t>(file_size), file) != static_cast<size_t>(file_size)) {
            memory_free(data);
            data = nullptr;
        }
    }
    fclose(file);
    size = static_cast<size_t>(file_size);
    return data;
}

void write_cmap(ByteBuffer& out, const GlyphInfo* glyphs, size_t count) {
    // Glyph ids are 1..count in codepoint order, so every subtable can be a "tiny" format
    struct Range {
        size_t first;
        size_t count;
        bool contiguous;
    };

    auto next_range = [&](size_t first) {
        Range range = { first, 1, true };
        while (first + range.count < count && range.count < CMAP_MAX_RANGE) {
            const GlyphInfo& candidate = glyphs[first + range.count];
            if (candidate.codepoint - glyphs[first].codepoint >= CMAP_MAX_RANGE) {
                break;
            }
            if (candidate.codepoint != glyphs[first].codepoint + range.count) {
                range.contiguous = false;
            }
            range.count++;
        }
        return range;
    };

    uint32_t range_count = 0;
    for (size_t i = 0; i < count; i += next_range(i).count) {
        range_count++;
    }

    const size_t record = out.beginRecord("cmap");
    out.appendU32(range_count);

    size_t data_offset = 12 + range_count * 16;
    for (size_t i = 0; i < count;) {
        const Range range = next_range(i);
        const uint32_t range_start = glyphs[i].codepoint;
        const uint32_t range_length = glyphs[i + range.count - 1].codepoint - range_start + 1;
        out.appendU32(range.contiguous ? 0 : static_cast<uint32_t>(data_offset));
        out.appendU32(range_start);
        out.appendU16(static_cast<uint16_t>(range_length));
        out.appendU16(static_cast<uint16_t>(i + 1));
        out.appendU16(range.contiguous ? 0 : static_cast<uint16_t>(range.count));
        out.appendU8(range.contiguous ? CMAP_FORMAT_0_TINY : CMAP_FORMAT_SPARSE_TINY);
        out.appendU8(0);
        if (!range.contiguous) {
            data_offset += (range.count * 2 + 3) & ~static_cast<size_t>(3);
        }
        i += range.count;
    }

    for (size_t i = 0; i < count;) {
        const Range range = next_range(i);
        if (!range.contiguous) {
            for (size_t j = 0; j < range.count; j++) {
                out.appendU16(static_cast<uint16_t>(glyphs[i + j].codepoint - glyphs[i].codepoint));
            }
            out.alignTo4();
        }
        i += range.count;
    }

    out.endRecord(record);
}

error_t generate(const BinFontGeneratorConfig* config, const stbtt_fontinfo& info, GlyphInfo* glyphs, ByteBuffer& out) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, config->size);

    // Collect glyphs, sorted by codepoint and without duplicates or missing glyphs
    size_t count = 0;
    for (size_t i = 0; i < config->codepoint_count; i++) {
        const int index = stbtt_FindGlyphIndex(&info, static_cast<int>(config->codepoints[i]));
        if (index != 0) {
            glyphs[count].codepoint = config->codepoints[i];
            glyphs[count].ttfIndex = index;
            count++;
        }
    }
    std::sort(glyphs, glyphs + count, [](const GlyphInfo& a, const GlyphInfo& b) { return a.codepoint < b.codepoint; });
    count = std::unique(glyphs, glyphs + count, [](const GlyphInfo& a, const GlyphInfo& b) { return a.codepoint == b.codepoint; }) - glyphs;
    if (count == 0 || count >= 0xFFFF) {
        return ERROR_NOT_SUPPORTED;
    }

    int32_t min_xy = 0;
    int32_t max_xy = 0;
    uint32_t max_wh = 0;
    uint32_t max_advance = 0;
    // Like lv_font_conv, ascent and descent are the extents of the included glyphs
    int32_t min_y = INT32_MAX;
    int32_t max_y = INT32_MIN;
    for (size_t i = 0; i < count; i++) {
        GlyphInfo& glyph = glyphs[i];
        int advance;
        int left_side_bearing;
        stbtt_GetGlyphHMetrics(&info, glyph.ttfIndex, &advance, &left_side_bearing);
        glyph.advanceX16 = static_cast<uint32_t>(std::lround(static_cast<float>(advance) * scale * 16.0f));
        stbtt_GetGlyphBitmapBox(&info, glyph.ttfIndex, scale, scale, &glyph.x0, &glyph.y0, &glyph.x1, &glyph.y1);

        const int32_t bottom = -glyph.y1;
        min_xy = std::min({ min_xy, static_cast<int32_t>(glyph.x0), bottom });
        max_xy = std::max({ max_xy, static_cast<int32_t>(glyph.x0), bottom });
        max_wh = std::max({ max_wh, static_cast<uint32_t>(glyph.x1 - glyph.x0), static_cast<uint32_t>(glyph.y1 - glyph.y0) });
        max_advance = std::max(max_advance, glyph.advanceX16);
        min_y = std::min(min_y, bottom);
        max_y = std::max(max_y, static_cast<int32_t>(-glyph.y0));
    }

    const uint8_t xy_bits = bits_for_signed(min_xy, max_xy);
    const uint8_t wh_bits = bits_for_unsigned(max_wh);
    const uint8_t advance_bits = bits_for_unsigned(max_advance);
    const uint32_t glyph_count = static_cast<uint32_t>(count) + 1;

    int typo_ascent = 0;
    int typo_descent = 0;
    int typo_line_gap = 0;
    stbtt_GetFontVMetricsOS2(&info, &typo_ascent, &typo_descent, &typo_line_gap);
    int underline_position = 0;
    int underline_thickness = 0;
    const stbtt_uint32 post = stbtt__find_table(info.data, info.fontstart, "post");
    if (post != 0) {
        underline_position = ttSHORT(info.data + post + 8);
        underline_thickness = ttSHORT(info.data + post + 10);
    }

    // head
    const size_t head = out.beginRecord("head");
    out.appendU32(1); // version
    out.appendU16(3); // additional tables: cmap, loca, glyf
    out.appendU16(config->size);
    out.appendU16(static_cast<uint16_t>(max_y));
    out.appendU16(static_cast<uint16_t>(min_y));
    out.appendU16(static_cast<uint16_t>(scale_metric(typo_ascent, scale)));
    out.appendU16(static_cast<uint16_t>(scale_metric(typo_descent, scale)));
    out.appendU16(static_cast<uint16_t>(scale_metric(typo_line_gap, scale)));
    out.appendU16(static_cast<uint16_t>(min_y));
    out.appendU16(static_cast<uint16_t>(max_y));
    out.appendU16(0); // default advance width
    out.appendU16(16); // kerning scale 1.0 in FP12.4
    out.appendU8(1); // loca: Offset32
    out.appendU8(glyph_count > 256 ? 1 : 0);
    out.appendU8(1); // advance width with 4 fractional bits
    out.appendU8(config->bpp);
    out.appendU8(xy_bits);
    out.appendU8(wh_bits);
    out.appendU8(advance_bits);
    out.appendU8(0); // no compression
    out.appendU8(0); // no subpixel rendering
    out.appendU8(0);
    out.appendU16(static_cast<uint16_t>(scale_metric(underline_position, scale)));
    out.appendU16(static_cast<uint16_t>(scale_metric(underline_thickness, scale)));
    out.endRecord(head);

    write_cmap(out, glyphs, count);

    const size_t loca = out.beginRecord("loca");
    out.appendU32(glyph_count);
    const size_t loca_entries = out.getSize();
    out.appendZeros(glyph_count * 4);
    out.endRecord(loca);

    const size_t glyf = out.beginRecord("glyf");
    uint8_t* bitmap = nullptr;
    size_t bitmap_capacity = 0;
    const uint32_t max_value = (1U << config->bpp) - 1;
    for (uint32_t id = 0; id < glyph_count && !out.hasFailed(); id++) {
        out.setU32(loca_entries + id * 4, static_cast<uint32_t>(out.getSize() - glyf));
        BitWriter writer(out);
        if (id == 0) {
            writer.write(0, advance_bits);
            writer.write(0, xy_bits);
            writer.write(0, xy_bits);
            writer.write(0, wh_bits);
            writer.write(0, wh_bits);
            writer.flush();
            continue;
        }

        const GlyphInfo& glyph = glyphs[id - 1];
        const int width = glyph.x1 - glyph.x0;
        const int height = glyph.y1 - glyph.y0;
        writer.write(glyph.advanceX16, advance_bits);
        writer.write(static_cast<uint32_t>(glyph.x0), xy_bits);
        writer.write(static_cast<uint32_t>(-glyph.y1), xy_bits);
        writer.write(static_cast<uint32_t>(width), wh_bits);
        writer.write(static_cast<uint32_t>(height), wh_bits);

        const size_t pixel_count = static_cast<size_t>(width) * height;
        if (pixel_count > 0) {
            if (pixel_count > bitmap_capacity) {
                memory_free(bitmap);
                bitmap = static_cast<uint8_t*>(memory_alloc_with_policy(pixel_count, &EXTERNAL_PREFERRED));
                if (bitmap == nullptr) {
                    return ERROR_OUT_OF_MEMORY;
                }
                bitmap_capacity = pixel_count;
            }
            stbtt_MakeGlyphBitmap(&info, bitmap, width, height, width, scale, scale, glyph.ttfIndex);
            for (size_t i = 0; i < pixel_count; i++) {
                writer.write((bitmap[i] * max_value + 127) / 255, config->bpp);
            }
        }
        writer.flush();
    }
    memory_free(bitmap);
    out.alignTo4();
    out.endRecord(glyf);

    return out.hasFailed() ? ERROR_OUT_OF_MEMORY : ERROR_NONE;
}

} // namespace

extern "C" {

error_t binfont_generate(const BinFontGeneratorConfig* config, uint8_t** out_data, size_t* out_size) {
    if (config->ttf_path == nullptr || config->size == 0 || config->bpp < 1 || config->bpp > 4 || config->codepoint_count == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    size_t ttf_size = 0;
    uint8_t* ttf = read_file(config->ttf_path, ttf_size);
    if (ttf == nullptr) {
        return ERROR_NOT_FOUND;
    }

    stbtt_fontinfo info;
    const int font_offset = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (font_offset < 0 || stbtt_InitFont(&info, ttf, font_offset) == 0) {
        memory_free(ttf);
        return ERROR_NOT_SUPPORTED;
    }

    auto* glyphs = static_cast<GlyphInfo*>(memory_alloc(config->codepoint_count * sizeof(GlyphInfo)));
    if (glyphs == nullptr) {
        memory_free(ttf);
        return ERROR_OUT_OF_MEMORY;
    }

    ByteBuffer out;
    const error_t error = generate(config, info, glyphs, out);
    memory_free(glyphs);
    memory_free(ttf);
    if (error != ERROR_NONE) {
        return error;
    }

    *out_size = out.getSize();
    *out_data = out.release();
    return ERROR_NONE;
}

}
