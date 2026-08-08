#include "Tactility/Bundle.h"

#include <tactility/bundle.h>

#include <vector>

namespace tt {

namespace {
::Bundle* as_kernel(void* handle) { return static_cast<::Bundle*>(handle); }
} // namespace

Bundle::Bundle() : handle(bundle_alloc()) {}

Bundle::Bundle(const Bundle& bundle) : handle(bundle_clone(as_kernel(bundle.handle))) {}

Bundle& Bundle::operator=(const Bundle& bundle) {
    if (this != &bundle) {
        ::Bundle* cloned = bundle_clone(as_kernel(bundle.handle));
        bundle_free(as_kernel(handle));
        handle = cloned;
    }
    return *this;
}

Bundle::~Bundle() {
    bundle_free(as_kernel(handle));
}

bool Bundle::getBool(const std::string& key) const {
    return bundle_get_bool(as_kernel(handle), key.c_str());
}

int32_t Bundle::getInt32(const std::string& key) const {
    return bundle_get_int32(as_kernel(handle), key.c_str());
}

int64_t Bundle::getInt64(const std::string& key) const {
    return bundle_get_int64(as_kernel(handle), key.c_str());
}

std::string Bundle::getString(const std::string& key) const {
    // bundle_get_string() needs a bounded buffer; grow and retry until it fits.
    std::vector<char> buffer(64);
    while (true) {
        error_t error = bundle_get_string(as_kernel(handle), key.c_str(), buffer.data(), buffer.size());
        if (error == ERROR_NONE) {
            return std::string(buffer.data());
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool Bundle::hasBool(const std::string& key) const {
    return bundle_has_bool(as_kernel(handle), key.c_str());
}

bool Bundle::hasInt32(const std::string& key) const {
    return bundle_has_int32(as_kernel(handle), key.c_str());
}

bool Bundle::hasInt64(const std::string& key) const {
    return bundle_has_int64(as_kernel(handle), key.c_str());
}

bool Bundle::hasString(const std::string& key) const {
    return bundle_has_string(as_kernel(handle), key.c_str());
}

bool Bundle::optBool(const std::string& key, bool& out) const {
    return bundle_opt_bool(as_kernel(handle), key.c_str(), &out);
}

bool Bundle::optInt32(const std::string& key, int32_t& out) const {
    return bundle_opt_int32(as_kernel(handle), key.c_str(), &out);
}

bool Bundle::optInt64(const std::string& key, int64_t& out) const {
    return bundle_opt_int64(as_kernel(handle), key.c_str(), &out);
}

bool Bundle::optString(const std::string& key, std::string& out) const {
    std::vector<char> buffer(64);
    while (true) {
        error_t error = bundle_opt_string(as_kernel(handle), key.c_str(), buffer.data(), buffer.size());
        if (error == ERROR_NONE) {
            out = buffer.data();
            return true;
        }
        if (error == ERROR_NOT_FOUND) {
            return false;
        }
        buffer.resize(buffer.size() * 2);
    }
}

void Bundle::putBool(const std::string& key, bool value) {
    bundle_put_bool(as_kernel(handle), key.c_str(), value);
}

void Bundle::putInt32(const std::string& key, int32_t value) {
    bundle_put_int32(as_kernel(handle), key.c_str(), value);
}

void Bundle::putInt64(const std::string& key, int64_t value) {
    bundle_put_int64(as_kernel(handle), key.c_str(), value);
}

void Bundle::putString(const std::string& key, const std::string& value) {
    bundle_put_string(as_kernel(handle), key.c_str(), value.c_str());
}

} // namespace
