/**
 * @brief key-value storage for general purpose.
 * Maps strings on a fixed set of data types.
 */
#pragma once

#include <cstdint>
#include <string>

namespace tt {

/**
 * A dictionary that maps keys (strings) onto several atomary types.
 * Thin C++ wrapper around TactilityKernel's C Bundle (tactility/bundle.h).
 */
class Bundle final {

    // Actually a TactilityKernel ::Bundle* (tactility/bundle.h), cast in Bundle.cpp - kept as
    // void* here rather than a forward-declared `struct Bundle*` so this header doesn't put a
    // second, unqualified `Bundle` name in scope: any TU with `using namespace tt;` in effect
    // (e.g. tests) would then find both `::Bundle` and `tt::Bundle` for a bare `Bundle` lookup
    // and fail with "reference to 'Bundle' is ambiguous".
    void* handle;

public:

    Bundle();

    Bundle(const Bundle& bundle);
    Bundle& operator=(const Bundle& bundle);

    ~Bundle();

    bool getBool(const std::string& key) const;
    int32_t getInt32(const std::string& key) const;
    int64_t getInt64(const std::string& key) const;
    std::string getString(const std::string& key) const;

    bool hasBool(const std::string& key) const;
    bool hasInt32(const std::string& key) const;
    bool hasInt64(const std::string& key) const;
    bool hasString(const std::string& key) const;

    bool optBool(const std::string& key, bool& out) const;
    bool optInt32(const std::string& key, int32_t& out) const;
    bool optInt64(const std::string& key, int64_t& out) const;
    bool optString(const std::string& key, std::string& out) const;

    void putBool(const std::string& key, bool value);
    void putInt32(const std::string& key, int32_t value);
    void putInt64(const std::string& key, int64_t value);
    void putString(const std::string& key, const std::string& value);
};

} // namespace
