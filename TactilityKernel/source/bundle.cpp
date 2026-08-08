// SPDX-License-Identifier: Apache-2.0
#include <tactility/bundle.h>

#include <cstring>
#include <new>
#include <string>
#include <unordered_map>

namespace {

enum class Type {
    Bool,
    Int32,
    Int64,
    String,
};

struct Value {
    Type type;
    union {
        bool value_bool;
        int32_t value_int32;
        int64_t value_int64;
    };
    std::string value_string;
};

} // namespace

// Definition of the opaque handle declared in tactility/bundle.h - C callers only ever see it
// through a Bundle* pointer, never its members.
struct Bundle {
    std::unordered_map<std::string, Value> entries;
};

extern "C" {

Bundle* bundle_alloc(void) {
    return new (std::nothrow) Bundle();
}

Bundle* bundle_clone(const Bundle* bundle) {
    auto* clone = new (std::nothrow) Bundle();
    if (clone == nullptr) {
        return nullptr;
    }
    clone->entries = bundle->entries;
    return clone;
}

void bundle_free(Bundle* bundle) {
    delete bundle;
}

bool bundle_get_bool(const Bundle* bundle, const char* key) {
    return bundle->entries.find(key)->second.value_bool;
}

int32_t bundle_get_int32(const Bundle* bundle, const char* key) {
    return bundle->entries.find(key)->second.value_int32;
}

int64_t bundle_get_int64(const Bundle* bundle, const char* key) {
    return bundle->entries.find(key)->second.value_int64;
}

error_t bundle_get_string(const Bundle* bundle, const char* key, char* out_value, size_t out_value_size) {
    const std::string& value = bundle->entries.find(key)->second.value_string;
    if (value.size() + 1 > out_value_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::memcpy(out_value, value.c_str(), value.size() + 1);
    return ERROR_NONE;
}

bool bundle_has_bool(const Bundle* bundle, const char* key) {
    auto entry = bundle->entries.find(key);
    return entry != bundle->entries.end() && entry->second.type == Type::Bool;
}

bool bundle_has_int32(const Bundle* bundle, const char* key) {
    auto entry = bundle->entries.find(key);
    return entry != bundle->entries.end() && entry->second.type == Type::Int32;
}

bool bundle_has_int64(const Bundle* bundle, const char* key) {
    auto entry = bundle->entries.find(key);
    return entry != bundle->entries.end() && entry->second.type == Type::Int64;
}

bool bundle_has_string(const Bundle* bundle, const char* key) {
    auto entry = bundle->entries.find(key);
    return entry != bundle->entries.end() && entry->second.type == Type::String;
}

bool bundle_opt_bool(const Bundle* bundle, const char* key, bool* out_value) {
    auto entry = bundle->entries.find(key);
    if (entry != bundle->entries.end() && entry->second.type == Type::Bool) {
        *out_value = entry->second.value_bool;
        return true;
    }
    return false;
}

bool bundle_opt_int32(const Bundle* bundle, const char* key, int32_t* out_value) {
    auto entry = bundle->entries.find(key);
    if (entry != bundle->entries.end() && entry->second.type == Type::Int32) {
        *out_value = entry->second.value_int32;
        return true;
    }
    return false;
}

bool bundle_opt_int64(const Bundle* bundle, const char* key, int64_t* out_value) {
    auto entry = bundle->entries.find(key);
    if (entry != bundle->entries.end() && entry->second.type == Type::Int64) {
        *out_value = entry->second.value_int64;
        return true;
    }
    return false;
}

error_t bundle_opt_string(const Bundle* bundle, const char* key, char* out_value, size_t out_value_size) {
    auto entry = bundle->entries.find(key);
    if (entry == bundle->entries.end() || entry->second.type != Type::String) {
        return ERROR_NOT_FOUND;
    }
    const std::string& value = entry->second.value_string;
    if (value.size() + 1 > out_value_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::memcpy(out_value, value.c_str(), value.size() + 1);
    return ERROR_NONE;
}

void bundle_put_bool(Bundle* bundle, const char* key, bool value) {
    bundle->entries[key] = Value { .type = Type::Bool, .value_bool = value, .value_string = "" };
}

void bundle_put_int32(Bundle* bundle, const char* key, int32_t value) {
    bundle->entries[key] = Value { .type = Type::Int32, .value_int32 = value, .value_string = "" };
}

void bundle_put_int64(Bundle* bundle, const char* key, int64_t value) {
    bundle->entries[key] = Value { .type = Type::Int64, .value_int64 = value, .value_string = "" };
}

void bundle_put_string(Bundle* bundle, const char* key, const char* value) {
    bundle->entries[key] = Value { .type = Type::String, .value_bool = false, .value_string = value };
}

} // extern "C"
