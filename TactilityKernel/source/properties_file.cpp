// SPDX-License-Identifier: Apache-2.0
#include <tactility/properties_file.h>
#include <tactility/filesystem/file_mutex.h>
#include <tactility/log.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>

constexpr auto* TAG = "properties_file";

namespace {

std::string trim(const std::string& value, const char* chars) {
    size_t start = value.find_first_not_of(chars);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = value.find_last_not_of(chars);
    return value.substr(start, end - start + 1);
}

bool split_key_value(const std::string& line, std::string& key, std::string& value) {
    size_t index = line.find('=');
    if (index == std::string::npos) {
        return false;
    }
    key = line.substr(0, index);
    value = line.substr(index + 1);
    return true;
}

} // namespace

// Definition of the opaque handle declared in tactility/properties_file.h - C callers only
// ever see it through a PropertiesFile* pointer, never its members.
struct PropertiesFile {
    std::string path;
    std::unordered_map<std::string, std::string> entries;
};

namespace {

// Missing file is not an error - a fresh instance just starts out empty and gets created on
// close(). Mirrors Tactility's loadPropertiesFile(): "#"-prefixed and blank lines are skipped;
// a "[section]" line becomes a literal prefix (verbatim, brackets included) prepended to every
// subsequent key, until the next "[section]" line replaces it.
// @return false if the file exists but a genuine I/O error interrupted opening or reading it
// (fgetc()'s EOF return doesn't by itself distinguish clean end-of-file from a read error -
// ferror() after the loop does); true otherwise, including for a missing file (ENOENT).
bool load_from_file(PropertiesFile* file) {
    FileMutex mutex {};
    file_mutex_get(&mutex, file->path.c_str());
    file_mutex_lock(&mutex);

    FILE* handle = std::fopen(file->path.c_str(), "r");
    if (handle == nullptr) {
        const int open_error = errno;
        file_mutex_unlock(&mutex);
        if (open_error == ENOENT) {
            return true;
        }
        LOG_E(TAG, "Failed to open %s", file->path.c_str());
        return false;
    }

    std::string key_prefix;
    std::string raw_line;
    uint32_t line_number = 0;

    auto flush_line = [&]() {
        line_number++;
        std::string trimmed_line = trim(raw_line, " \t\r\n");
        raw_line.clear();

        if (trimmed_line.empty() || trimmed_line.starts_with("#")) {
            return;
        }
        if (trimmed_line.starts_with("[")) {
            key_prefix = trimmed_line;
            return;
        }

        std::string key, value;
        if (!split_key_value(trimmed_line, key, value)) {
            LOG_E(TAG, "Failed to parse line %u of %s (skipped)", line_number, file->path.c_str());
            return;
        }
        file->entries[key_prefix + trim(key, " \t")] = trim(value, " \t");
    };

    int c;
    while ((c = std::fgetc(handle)) != EOF) {
        if (c == '\n') {
            flush_line();
        } else {
            raw_line += static_cast<char>(c);
        }
    }
    flush_line();

    bool read_ok = std::ferror(handle) == 0;
    std::fclose(handle);
    file_mutex_unlock(&mutex);

    if (!read_ok) {
        LOG_E(TAG, "Failed to read %s", file->path.c_str());
    }
    return read_ok;
}

// Writes to a temporary file in the same directory, then atomically replaces the real path -
// opening the real path directly with "w" would truncate it immediately, so any failure
// partway through (full filesystem, I/O error, a reset before close) would discard the
// previously-good content instead of leaving it intact. Same directory so rename() stays on one
// filesystem, which is what makes it atomic.
// @return true if the backing file was fully replaced with the current entries; false (leaving
// the previous on-disk content untouched) if any step failed.
bool save_to_file(const PropertiesFile* file) {
    FileMutex mutex {};
    file_mutex_get(&mutex, file->path.c_str());
    file_mutex_lock(&mutex);

    std::string temp_path = file->path + ".tmp";

    FILE* handle = std::fopen(temp_path.c_str(), "w");
    if (handle == nullptr) {
        LOG_E(TAG, "Failed to open %s", temp_path.c_str());
        file_mutex_unlock(&mutex);
        return false;
    }

    for (const auto& [key, value] : file->entries) {
        std::fprintf(handle, "%s=%s\n", key.c_str(), value.c_str());
    }

    // Order matters: ferror()/fflush() need the still-open handle, fclose() consumes it.
    bool write_ok = std::ferror(handle) == 0;
    bool flush_ok = std::fflush(handle) == 0;
    bool close_ok = std::fclose(handle) == 0;

    if (!write_ok || !flush_ok || !close_ok) {
        LOG_E(TAG, "Failed to write %s", temp_path.c_str());
        std::remove(temp_path.c_str());
        file_mutex_unlock(&mutex);
        return false;
    }

    if (std::rename(temp_path.c_str(), file->path.c_str()) != 0) {
        LOG_E(TAG, "Failed to replace %s", file->path.c_str());
        std::remove(temp_path.c_str());
        file_mutex_unlock(&mutex);
        return false;
    }

    file_mutex_unlock(&mutex);
    return true;
}

} // namespace

extern "C" {

PropertiesFile* properties_file_open(const char* path) {
    auto* file = new (std::nothrow) PropertiesFile();
    if (file == nullptr) {
        return nullptr;
    }
    file->path = path;
    if (!load_from_file(file)) {
        delete file;
        return nullptr;
    }
    return file;
}

error_t properties_file_close(PropertiesFile* file) {
    bool saved = save_to_file(file);
    delete file;
    return saved ? ERROR_NONE : ERROR_RESOURCE;
}

bool properties_file_has(const PropertiesFile* file, const char* key) {
    return file->entries.contains(key);
}

error_t properties_file_get(const PropertiesFile* file, const char* key, char* out_value, size_t out_value_size) {
    auto entry = file->entries.find(key);
    if (entry == file->entries.end()) {
        return ERROR_NOT_FOUND;
    }
    const std::string& value = entry->second;
    if (value.size() + 1 > out_value_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::memcpy(out_value, value.c_str(), value.size() + 1);
    return ERROR_NONE;
}

void properties_file_set(PropertiesFile* file, const char* key, const char* value) {
    file->entries[key] = value;
}

void properties_file_for_each(const PropertiesFile* file, PropertiesFileVisitorFn visitor, void* context) {
    for (const auto& [key, value] : file->entries) {
        visitor(key.c_str(), value.c_str(), context);
    }
}

} // extern "C"
