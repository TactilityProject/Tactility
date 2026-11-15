// SPDX-License-Identifier: Apache-2.0
#include <localisation/catalog.h>

#include <cstring>

namespace {

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint32_t hash_bytes(uint32_t hash, const char* text) {
    for (; *text != '\0'; text++) {
        hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619u;
    }
    return hash;
}

bool key_equals(const char* key, const char* context, const char* text) {
    if (context != nullptr) {
        const size_t context_length = strlen(context);
        if (strncmp(key, context, context_length) != 0 || key[context_length] != CATALOG_CONTEXT_SEPARATOR) {
            return false;
        }
        key += context_length + 1;
    }
    return strcmp(key, text) == 0;
}

}

uint32_t catalog_hash(const char* context, const char* text) {
    uint32_t hash = 2166136261u;
    if (context != nullptr) {
        hash = hash_bytes(hash, context);
        hash = (hash ^ static_cast<uint8_t>(CATALOG_CONTEXT_SEPARATOR)) * 16777619u;
    }
    return hash_bytes(hash, text);
}

bool catalog_validate(const void* data, size_t size) {
    if (data == nullptr || size < CATALOG_HEADER_SIZE) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (memcmp(bytes, "TLC1", 4) != 0 || read_u32(bytes + 4) != CATALOG_VERSION) {
        return false;
    }
    const uint32_t count = read_u32(bytes + 8);
    const size_t blob_start = CATALOG_HEADER_SIZE + static_cast<size_t>(count) * CATALOG_ENTRY_SIZE;
    if (count > (size - CATALOG_HEADER_SIZE) / CATALOG_ENTRY_SIZE || blob_start > size) {
        return false;
    }
    const size_t blob_size = size - blob_start;
    // The blob must end with a NUL, so every offset that is inside of it points to a terminated string
    if (count > 0 && (blob_size == 0 || bytes[size - 1] != '\0')) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* entry = bytes + CATALOG_HEADER_SIZE + static_cast<size_t>(i) * CATALOG_ENTRY_SIZE;
        if (read_u32(entry + 4) >= blob_size || read_u32(entry + 8) >= blob_size) {
            return false;
        }
    }
    return true;
}

const char* catalog_find(const void* data, const char* context, const char* text) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint32_t count = read_u32(bytes + 8);
    const uint8_t* entries = bytes + CATALOG_HEADER_SIZE;
    const char* blob = reinterpret_cast<const char*>(entries + static_cast<size_t>(count) * CATALOG_ENTRY_SIZE);
    const uint32_t hash = catalog_hash(context, text);

    // First entry with this hash
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (read_u32(entries + static_cast<size_t>(middle) * CATALOG_ENTRY_SIZE) < hash) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    for (uint32_t i = low; i < count; i++) {
        const uint8_t* entry = entries + static_cast<size_t>(i) * CATALOG_ENTRY_SIZE;
        if (read_u32(entry) != hash) {
            break;
        }
        if (key_equals(blob + read_u32(entry + 4), context, text)) {
            return blob + read_u32(entry + 8);
        }
    }
    return nullptr;
}
