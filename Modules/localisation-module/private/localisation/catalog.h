// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Binary catalog ("<locale>.tlc"), little endian:
 *   Header: "TLC1", u32 version (1), u32 count, u32 reserved (flags, 0)
 *   Entries: count * { u32 hash, u32 key offset, u32 value offset }, sorted by hash
 *   Blob: NUL terminated strings. Offsets are relative to the start of the blob.
 * The key is the text, or "<context>\x04<text>" when it has a context. The hash is FNV-1a (32 bit) of the key.
 */
constexpr uint32_t CATALOG_VERSION = 1;
constexpr size_t CATALOG_HEADER_SIZE = 16;
constexpr size_t CATALOG_ENTRY_SIZE = 12;
constexpr char CATALOG_CONTEXT_SEPARATOR = '\x04';

uint32_t catalog_hash(const char* context, const char* text);

/** @return true when the data is a well formed catalog, with all offsets inside of it */
bool catalog_validate(const void* data, size_t size);

/** @return the translation inside of the catalog data, or nullptr. @a context can be nullptr. The data must be valid. */
const char* catalog_find(const void* data, const char* context, const char* text);
