// SPDX-License-Identifier: Apache-2.0
#include <binfont/binfont.h>
#include <binfont_private.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/memory.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

constexpr size_t RECORD_HEADER_SIZE = 8;
constexpr size_t HEAD_MIN_SIZE = 48;
constexpr size_t CMAP_SUBTABLE_HEADER_SIZE = 16;
constexpr size_t GLYPH_HEADER_MAX_BYTES = 16;

enum CmapFormat : uint8_t {
    CMAP_FORMAT_0 = 0,
    CMAP_FORMAT_SPARSE = 1,
    CMAP_FORMAT_0_TINY = 2,
    CMAP_FORMAT_SPARSE_TINY = 3,
};

enum Compression : uint8_t {
    COMPRESSION_NONE = 0,
    COMPRESSION_RLE_PREFILTER = 1,
    COMPRESSION_RLE = 2,
};

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

int16_t read_i16(const uint8_t* p) {
    return static_cast<int16_t>(read_u16(p));
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool has_marker(const uint8_t* record, const char* marker) {
    return memcmp(record + 4, marker, 4) == 0;
}

/** MSB-first bit reader. Bits past the end read as 0. */
class BitReader {
    const uint8_t* data;
    size_t sizeBits;
    size_t position;

public:
    BitReader(const uint8_t* data, size_t size, size_t bitOffset = 0) : data(data), sizeBits(size * 8), position(bitOffset) {}

    uint32_t read(uint8_t bits) {
        uint32_t value = 0;
        for (uint8_t i = 0; i < bits; i++) {
            value <<= 1;
            if (position < sizeBits) {
                value |= (data[position >> 3] >> (7 - (position & 7))) & 1U;
            }
            position++;
        }
        return value;
    }

    int32_t readSigned(uint8_t bits) {
        if (bits == 0) {
            return 0;
        }
        const uint32_t value = read(bits);
        if (value & (1U << (bits - 1))) {
            return static_cast<int32_t>(value | (~0U << bits));
        }
        return static_cast<int32_t>(value);
    }
};

/** Decoder for the modified I3BN RLE stream of lv_font_conv. */
class RleReader {
    enum class State { Single, Repeated, Counter };

    BitReader& bits;
    uint8_t bpp;
    State state = State::Single;
    bool first = true;
    uint8_t previous = 0;
    uint32_t count = 0;

    uint8_t readSingle() {
        previous = static_cast<uint8_t>(bits.read(bpp));
        state = State::Single;
        return previous;
    }

public:
    RleReader(BitReader& bits, uint8_t bpp) : bits(bits), bpp(bpp) {}

    uint8_t next() {
        switch (state) {
            case State::Single: {
                const auto value = static_cast<uint8_t>(bits.read(bpp));
                if (!first && value == previous) {
                    count = 0;
                    state = State::Repeated;
                }
                first = false;
                previous = value;
                return value;
            }
            case State::Repeated: {
                count++;
                if (bits.read(1) == 0) {
                    return readSingle();
                }
                if (count == 11) {
                    count = bits.read(6);
                    if (count != 0) {
                        state = State::Counter;
                    } else {
                        return readSingle();
                    }
                }
                return previous;
            }
            case State::Counter:
                count--;
                if (count == 0) {
                    return readSingle();
                }
                return previous;
        }
        return 0;
    }
};

struct Header {
    uint16_t fontSize;
    int16_t ascent;
    int16_t descent;
    uint16_t defaultAdvanceWidth;
    uint16_t kerningScale;
    uint8_t indexToLocFormat;
    uint8_t glyphIdFormat;
    uint8_t advanceWidthFormat;
    uint8_t bpp;
    uint8_t xyBits;
    uint8_t whBits;
    uint8_t advanceWidthBits;
    uint8_t compression;
    uint8_t subpixelMode;
    int16_t underlinePosition;
    uint16_t underlineThickness;
};

} // namespace

struct BinFont {
    Header header;
    /** The whole file in memory mode, nullptr in stream mode */
    const uint8_t* data;
    bool ownsData;
    /** Stream mode only: the file path, and the file while it's open (guarded by mutex) */
    char* path;
    FILE* file;
    uint32_t sessionCount;
    Mutex mutex;
    /** Owned copy of the cmap/loca/kern records in stream mode */
    uint8_t* tables;
    /** Each record pointer includes its 8-byte record header */
    const uint8_t* cmap;
    size_t cmapSize;
    const uint8_t* loca;
    size_t locaSize;
    uint32_t locaCount;
    const uint8_t* kern;
    size_t kernSize;
    size_t glyfOffset;
    size_t glyfSize;
};

namespace {

bool read_at(BinFont* font, size_t offset, uint8_t* out, size_t size) {
    if (fseek(font->file, static_cast<long>(offset), SEEK_SET) != 0) {
        return false;
    }
    return fread(out, 1, size, font->file) == size;
}

bool parse_header(const uint8_t* record, size_t size, Header& header) {
    if (size < HEAD_MIN_SIZE || !has_marker(record, "head")) {
        return false;
    }
    header.fontSize = read_u16(record + 14);
    header.ascent = read_i16(record + 16);
    header.descent = read_i16(record + 18);
    header.defaultAdvanceWidth = read_u16(record + 30);
    header.kerningScale = read_u16(record + 32);
    header.indexToLocFormat = record[34];
    header.glyphIdFormat = record[35];
    header.advanceWidthFormat = record[36];
    header.bpp = record[37];
    header.xyBits = record[38];
    header.whBits = record[39];
    header.advanceWidthBits = record[40];
    header.compression = record[41];
    header.subpixelMode = record[42];
    header.underlinePosition = read_i16(record + 44);
    header.underlineThickness = read_u16(record + 46);

    return header.bpp >= 1 && header.bpp <= 4 &&
        header.compression <= COMPRESSION_RLE &&
        header.subpixelMode == 0 &&
        header.indexToLocFormat <= 1 &&
        header.xyBits <= 16 && header.whBits <= 16 && header.advanceWidthBits <= 32;
}

bool validate_cmap(const uint8_t* cmap, size_t size) {
    if (size < 12) {
        return false;
    }
    const uint32_t count = read_u32(cmap + 8);
    if (count > (size - 12) / CMAP_SUBTABLE_HEADER_SIZE) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* subtable = cmap + 12 + i * CMAP_SUBTABLE_HEADER_SIZE;
        const size_t data_offset = read_u32(subtable);
        const size_t range_length = read_u16(subtable + 8);
        const size_t entries = read_u16(subtable + 12);
        size_t data_size;
        switch (subtable[14]) {
            case CMAP_FORMAT_0: data_size = range_length; break;
            case CMAP_FORMAT_SPARSE: data_size = entries * 4; break;
            case CMAP_FORMAT_0_TINY: data_size = 0; break;
            case CMAP_FORMAT_SPARSE_TINY: data_size = entries * 2; break;
            default: return false;
        }
        if (data_size > 0 && (data_offset > size || data_size > size - data_offset)) {
            return false;
        }
    }
    return true;
}

bool validate_kern(const uint8_t* kern, size_t size, uint8_t glyphIdFormat) {
    if (size < 16) {
        return false;
    }
    const uint8_t format = kern[8];
    if (format == 0) {
        const size_t count = read_u32(kern + 12);
        const size_t entry_size = (glyphIdFormat ? 4 : 2) + 1;
        return count <= (size - 16) / entry_size;
    } else if (format == 3) {
        const size_t glyph_count = read_u16(kern + 12);
        const size_t left_count = kern[14];
        const size_t right_count = kern[15];
        return 16 + glyph_count * 2 + left_count * right_count <= size;
    }
    return false;
}

/**
 * Locates all tables. In memory mode the table pointers point into font->data, in stream mode
 * the lookup tables are copied into font->tables.
 */
error_t parse_layout(BinFont* font, size_t fileSize) {
    uint8_t record_header[RECORD_HEADER_SIZE];
    uint8_t head[64];

    auto read = [font](size_t offset, uint8_t* out, size_t size) {
        if (font->data != nullptr) {
            memcpy(out, font->data + offset, size);
            return true;
        }
        return read_at(font, offset, out, size);
    };

    // head
    if (fileSize < RECORD_HEADER_SIZE || !read(0, record_header, RECORD_HEADER_SIZE)) {
        return ERROR_NOT_SUPPORTED;
    }
    const size_t head_size = read_u32(record_header);
    if (head_size < HEAD_MIN_SIZE || head_size > fileSize) {
        return ERROR_NOT_SUPPORTED;
    }
    if (!read(0, head, HEAD_MIN_SIZE) || !parse_header(head, HEAD_MIN_SIZE, font->header)) {
        return ERROR_NOT_SUPPORTED;
    }

    // cmap, loca, glyf and optionally kern follow each other
    size_t offsets[4] = {};
    size_t sizes[4] = {};
    const char* markers[4] = { "cmap", "loca", "glyf", "kern" };
    size_t offset = head_size;
    for (int i = 0; i < 4; i++) {
        if (offset > fileSize || fileSize - offset < RECORD_HEADER_SIZE || !read(offset, record_header, RECORD_HEADER_SIZE)) {
            if (i == 3) {
                break; // kern is optional
            }
            return ERROR_NOT_SUPPORTED;
        }
        const size_t size = read_u32(record_header);
        if (!has_marker(record_header, markers[i]) || size < RECORD_HEADER_SIZE || size > fileSize - offset) {
            if (i == 3) {
                break;
            }
            return ERROR_NOT_SUPPORTED;
        }
        offsets[i] = offset;
        sizes[i] = size;
        offset += size;
    }

    font->glyfOffset = offsets[2];
    font->glyfSize = sizes[2];

    if (font->data != nullptr) {
        font->cmap = font->data + offsets[0];
        font->loca = font->data + offsets[1];
        font->kern = sizes[3] ? font->data + offsets[3] : nullptr;
    } else {
        font->tables = static_cast<uint8_t*>(memory_alloc(sizes[0] + sizes[1] + sizes[3]));
        if (font->tables == nullptr) {
            return ERROR_OUT_OF_MEMORY;
        }
        uint8_t* cursor = font->tables;
        for (int i : { 0, 1, 3 }) {
            if (sizes[i] > 0 && !read_at(font, offsets[i], cursor, sizes[i])) {
                return ERROR_NOT_SUPPORTED;
            }
            cursor += sizes[i];
        }
        font->cmap = font->tables;
        font->loca = font->tables + sizes[0];
        font->kern = sizes[3] ? font->tables + sizes[0] + sizes[1] : nullptr;
    }
    font->cmapSize = sizes[0];
    font->locaSize = sizes[1];
    font->kernSize = sizes[3];

    if (!validate_cmap(font->cmap, font->cmapSize)) {
        return ERROR_NOT_SUPPORTED;
    }

    if (font->locaSize < 12) {
        return ERROR_NOT_SUPPORTED;
    }
    font->locaCount = read_u32(font->loca + 8);
    const size_t loca_entry_size = font->header.indexToLocFormat ? 4 : 2;
    if (font->locaCount > (font->locaSize - 12) / loca_entry_size) {
        return ERROR_NOT_SUPPORTED;
    }

    if (font->kern != nullptr && !validate_kern(font->kern, font->kernSize, font->header.glyphIdFormat)) {
        font->kern = nullptr;
        font->kernSize = 0;
    }

    return ERROR_NONE;
}

BinFont* create_font() {
    auto* font = static_cast<BinFont*>(memory_calloc(1, sizeof(BinFont)));
    if (font != nullptr) {
        mutex_construct(&font->mutex);
    }
    return font;
}

uint32_t find_glyph_id(const BinFont* font, uint32_t codepoint) {
    const uint8_t* cmap = font->cmap;
    const uint32_t count = read_u32(cmap + 8);

    // Find the last subtable that starts at or before the codepoint
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high) {
        const uint32_t mid = (low + high) / 2;
        if (read_u32(cmap + 12 + mid * CMAP_SUBTABLE_HEADER_SIZE + 4) <= codepoint) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low == 0) {
        return 0;
    }

    const uint8_t* subtable = cmap + 12 + (low - 1) * CMAP_SUBTABLE_HEADER_SIZE;
    const uint32_t data_offset = read_u32(subtable);
    const uint32_t range_start = read_u32(subtable + 4);
    const uint16_t range_length = read_u16(subtable + 8);
    const uint16_t glyph_id_start = read_u16(subtable + 10);
    const uint16_t entries = read_u16(subtable + 12);
    const uint32_t relative = codepoint - range_start;
    if (relative >= range_length) {
        return 0;
    }

    switch (subtable[14]) {
        case CMAP_FORMAT_0:
            return glyph_id_start + cmap[data_offset + relative];
        case CMAP_FORMAT_0_TINY:
            return glyph_id_start + relative;
        case CMAP_FORMAT_SPARSE:
        case CMAP_FORMAT_SPARSE_TINY: {
            const uint8_t* list = cmap + data_offset;
            uint32_t list_low = 0;
            uint32_t list_high = entries;
            while (list_low < list_high) {
                const uint32_t mid = (list_low + list_high) / 2;
                const uint16_t value = read_u16(list + mid * 2);
                if (value == relative) {
                    if (subtable[14] == CMAP_FORMAT_SPARSE_TINY) {
                        return glyph_id_start + mid;
                    }
                    return glyph_id_start + read_u16(list + entries * 2 + mid * 2);
                } else if (value < relative) {
                    list_low = mid + 1;
                } else {
                    list_high = mid;
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

/** Gets the offset and size of a glyph record, relative to the glyf data (after its record header). */
bool get_glyph_location(const BinFont* font, uint32_t glyphId, size_t& offset, size_t& size) {
    if (glyphId == 0 || glyphId >= font->locaCount) {
        return false;
    }
    const uint8_t* entries = font->loca + 12;
    auto entry = [&](uint32_t index) -> size_t {
        return font->header.indexToLocFormat ? read_u32(entries + index * 4) : read_u16(entries + index * 2);
    };
    const size_t start = entry(glyphId);
    const size_t end = (glyphId + 1 < font->locaCount) ? entry(glyphId + 1) : font->glyfSize;
    if (start < RECORD_HEADER_SIZE || end < start || end > font->glyfSize) {
        return false;
    }
    offset = start;
    size = end - start;
    return true;
}

/** Reads from the font file in stream mode. Outside of a session, the file is opened for this read only. */
bool read_glyph_data(BinFont* font, size_t offset, uint8_t* out, size_t size) {
    mutex_lock(&font->mutex);
    const bool temporary = font->file == nullptr;
    if (temporary) {
        font->file = fopen(font->path, "rb");
    }
    const bool success = font->file != nullptr && read_at(font, offset, out, size);
    if (temporary && font->file != nullptr) {
        fclose(font->file);
        font->file = nullptr;
    }
    mutex_unlock(&font->mutex);
    return success;
}

/**
 * Provides the record of a glyph. In memory mode it points into the font data, in stream mode it
 * is read into buffer (bufferSize bytes) or, when that is too small, into an allocation returned
 * via allocated (to be freed by the caller).
 */
bool get_glyph_record(BinFont* font, uint32_t glyphId, size_t maxSize, uint8_t* buffer, size_t bufferSize, const uint8_t*& record, size_t& size, uint8_t*& allocated) {
    size_t offset;
    allocated = nullptr;
    if (!get_glyph_location(font, glyphId, offset, size)) {
        return false;
    }
    if (font->data != nullptr) {
        record = font->data + font->glyfOffset + offset;
        return true;
    }

    if (size > maxSize) {
        size = maxSize;
    }
    uint8_t* target = buffer;
    if (size > bufferSize) {
        allocated = static_cast<uint8_t*>(memory_alloc(size));
        if (allocated == nullptr) {
            return false;
        }
        target = allocated;
    }

    const bool success = read_glyph_data(font, font->glyfOffset + offset, target, size);

    if (!success) {
        memory_free(allocated);
        allocated = nullptr;
        return false;
    }
    record = target;
    return true;
}

void parse_glyph(const Header& header, BitReader& reader, uint32_t glyphId, BinFontGlyph* out) {
    uint32_t advance = header.advanceWidthBits ? reader.read(header.advanceWidthBits) : header.defaultAdvanceWidth;
    if (header.advanceWidthFormat == 0) {
        advance *= 16;
    }
    out->glyph_id = glyphId;
    out->advance_x16 = advance;
    out->x = static_cast<int16_t>(reader.readSigned(header.xyBits));
    out->y = static_cast<int16_t>(reader.readSigned(header.xyBits));
    out->width = static_cast<uint16_t>(reader.read(header.whBits));
    out->height = static_cast<uint16_t>(reader.read(header.whBits));
}

/** Opens a file for reading and determines its size */
error_t open_file(const char* path, FILE*& file, size_t& size) {
    file = fopen(path, "rb");
    if (file == nullptr) {
        return ERROR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ERROR_NOT_SUPPORTED;
    }
    const long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ERROR_NOT_SUPPORTED;
    }
    size = static_cast<size_t>(file_size);
    return ERROR_NONE;
}

/** Loads the lookup tables from an open file, then closes it. Glyph data is read on demand. */
error_t open_streaming(const char* path, FILE* file, size_t size, BinFont** out) {
    BinFont* font = create_font();
    if (font == nullptr) {
        fclose(file);
        return ERROR_OUT_OF_MEMORY;
    }
    font->file = file;

    const size_t path_size = strlen(path) + 1;
    font->path = static_cast<char*>(memory_alloc(path_size));
    if (font->path == nullptr) {
        binfont_close(font);
        return ERROR_OUT_OF_MEMORY;
    }
    memcpy(font->path, path, path_size);

    const error_t error = parse_layout(font, size);
    fclose(font->file);
    font->file = nullptr;
    if (error != ERROR_NONE) {
        binfont_close(font);
        return error;
    }
    *out = font;
    return ERROR_NONE;
}

} // namespace

extern "C" {

error_t binfont_open_memory(const void* data, size_t size, bool take_ownership, BinFont** out) {
    BinFont* font = create_font();
    if (font == nullptr) {
        if (take_ownership) {
            memory_free(const_cast<void*>(data));
        }
        return ERROR_OUT_OF_MEMORY;
    }
    font->data = static_cast<const uint8_t*>(data);
    font->ownsData = take_ownership;

    const error_t error = parse_layout(font, size);
    if (error != ERROR_NONE) {
        binfont_close(font);
        return error;
    }
    *out = font;
    return ERROR_NONE;
}

error_t binfont_open_file(const char* path, BinFont** out) {
    FILE* file;
    size_t size;
    const error_t open_error = open_file(path, file, size);
    if (open_error != ERROR_NONE) {
        return open_error;
    }

    const uint16_t memory_capability = memory_external_total() > 0 ? MEMORY_CAPABILITY_EXTERNAL : MEMORY_CAPABILITY_INTERNAL;
    const MemoryPolicy policy = { .required = memory_capability, .desired = 0, .alignment = 0 };
    auto* data = static_cast<uint8_t*>(memory_alloc_with_policy(size, &policy));
    if (data == nullptr) {
        return open_streaming(path, file, size, out);
    }

    const bool success = fread(data, 1, size, file) == size;
    fclose(file);
    if (!success) {
        memory_free(data);
        return ERROR_NOT_SUPPORTED;
    }
    return binfont_open_memory(data, size, true, out);
}

error_t binfont_open_file_streaming(const char* path, BinFont** out) {
    FILE* file;
    size_t size;
    const error_t open_error = open_file(path, file, size);
    if (open_error != ERROR_NONE) {
        return open_error;
    }
    return open_streaming(path, file, size, out);
}

error_t binfont_begin(BinFont* font) {
    if (font->data != nullptr) {
        return ERROR_NONE;
    }
    mutex_lock(&font->mutex);
    if (font->sessionCount == 0) {
        font->file = fopen(font->path, "rb");
        if (font->file == nullptr) {
            mutex_unlock(&font->mutex);
            return ERROR_NOT_FOUND;
        }
    }
    font->sessionCount++;
    mutex_unlock(&font->mutex);
    return ERROR_NONE;
}

void binfont_end(BinFont* font) {
    if (font->data != nullptr) {
        return;
    }
    mutex_lock(&font->mutex);
    if (font->sessionCount > 0) {
        font->sessionCount--;
        if (font->sessionCount == 0) {
            fclose(font->file);
            font->file = nullptr;
        }
    }
    mutex_unlock(&font->mutex);
}

void binfont_close(BinFont* font) {
    if (font->ownsData) {
        memory_free(const_cast<uint8_t*>(font->data));
    }
    if (font->file != nullptr) {
        fclose(font->file);
    }
    memory_free(font->path);
    memory_free(font->tables);
    mutex_destruct(&font->mutex);
    memory_free(font);
}

void binfont_get_metrics(const BinFont* font, BinFontMetrics* out) {
    const Header& header = font->header;
    out->size = header.fontSize;
    out->ascent = header.ascent;
    out->descent = header.descent;
    out->line_height = static_cast<uint16_t>(header.ascent - header.descent);
    out->base_line = static_cast<int16_t>(-header.descent);
    out->underline_position = header.underlinePosition;
    out->underline_thickness = header.underlineThickness;
    out->bpp = header.bpp;
}

bool binfont_get_glyph(BinFont* font, uint32_t codepoint, BinFontGlyph* out) {
    const uint32_t glyph_id = find_glyph_id(font, codepoint);
    uint8_t buffer[GLYPH_HEADER_MAX_BYTES];
    const uint8_t* record;
    size_t size;
    uint8_t* allocated;
    if (!get_glyph_record(font, glyph_id, sizeof(buffer), buffer, sizeof(buffer), record, size, allocated)) {
        return false;
    }
    BitReader reader(record, size);
    parse_glyph(font->header, reader, glyph_id, out);
    return true;
}

int32_t binfont_get_kerning_x16(BinFont* font, uint32_t left_glyph_id, uint32_t right_glyph_id) {
    const uint8_t* kern = font->kern;
    if (kern == nullptr) {
        return 0;
    }

    int8_t value = 0;
    if (kern[8] == 0) {
        const uint32_t count = read_u32(kern + 12);
        const uint8_t* pairs = kern + 16;
        const bool wide = font->header.glyphIdFormat != 0;
        const size_t pair_size = wide ? 4 : 2;
        const uint32_t key = (left_glyph_id << 16) | right_glyph_id;
        uint32_t low = 0;
        uint32_t high = count;
        while (low < high) {
            const uint32_t mid = (low + high) / 2;
            const uint8_t* pair = pairs + mid * pair_size;
            const uint32_t left = wide ? read_u16(pair) : pair[0];
            const uint32_t right = wide ? read_u16(pair + 2) : pair[1];
            const uint32_t mid_key = (left << 16) | right;
            if (mid_key == key) {
                value = static_cast<int8_t>(pairs[count * pair_size + mid]);
                break;
            } else if (mid_key < key) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
    } else {
        const uint16_t glyph_count = read_u16(kern + 12);
        const uint8_t right_count = kern[15];
        if (left_glyph_id >= glyph_count || right_glyph_id >= glyph_count) {
            return 0;
        }
        const uint8_t* left_classes = kern + 16;
        const uint8_t* right_classes = left_classes + glyph_count;
        const uint8_t* values = right_classes + glyph_count;
        const uint8_t left_class = left_classes[left_glyph_id];
        const uint8_t right_class = right_classes[right_glyph_id];
        if (left_class == 0 || right_class == 0 || left_class > kern[14] || right_class > right_count) {
            return 0;
        }
        value = static_cast<int8_t>(values[(left_class - 1) * right_count + (right_class - 1)]);
    }

    return (static_cast<int32_t>(value) * font->header.kerningScale) >> 4;
}

error_t binfont_get_glyph_bitmap(BinFont* font, const BinFontGlyph* glyph, uint8_t* out, size_t stride) {
    if (stride < glyph->width) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (glyph->width == 0 || glyph->height == 0) {
        return ERROR_NONE;
    }

    const uint8_t* record;
    size_t size;
    uint8_t* allocated;
    if (!get_glyph_record(font, glyph->glyph_id, SIZE_MAX, nullptr, 0, record, size, allocated)) {
        return ERROR_RESOURCE;
    }

    const Header& header = font->header;
    const uint8_t bpp = header.bpp;
    const size_t header_bits = header.advanceWidthBits + 2U * header.xyBits + 2U * header.whBits;
    BitReader reader(record, size, header_bits);

    // Raw pixel values first, so the XOR prefilter can work on the previous line
    if (header.compression == COMPRESSION_NONE) {
        for (uint16_t y = 0; y < glyph->height; y++) {
            uint8_t* line = out + y * stride;
            for (uint16_t x = 0; x < glyph->width; x++) {
                line[x] = static_cast<uint8_t>(reader.read(bpp));
            }
        }
    } else {
        RleReader rle(reader, bpp);
        const bool prefilter = header.compression == COMPRESSION_RLE_PREFILTER;
        for (uint16_t y = 0; y < glyph->height; y++) {
            uint8_t* line = out + y * stride;
            const uint8_t* previous = line - stride;
            for (uint16_t x = 0; x < glyph->width; x++) {
                const uint8_t value = rle.next();
                line[x] = (prefilter && y > 0) ? (value ^ previous[x]) : value;
            }
        }
    }

    const uint32_t max_value = (1U << bpp) - 1;
    for (uint16_t y = 0; y < glyph->height; y++) {
        uint8_t* line = out + y * stride;
        for (uint16_t x = 0; x < glyph->width; x++) {
            line[x] = static_cast<uint8_t>(line[x] * 255U / max_value);
        }
    }

    memory_free(allocated);
    return ERROR_NONE;
}

}
