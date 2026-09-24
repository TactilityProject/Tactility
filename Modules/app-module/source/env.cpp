// SPDX-License-Identifier: Apache-2.0
#include <app/env.h>
#include <app/private/env_internal.h>
#include <app/private/ledger.h>
#include <app/scheduler.h>

#include <tactility/concurrent/mutex.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool is_valid_name(const char* name) {
    return name != nullptr && name[0] != '\0' && strchr(name, '=') == nullptr;
}

// "NAME=..." prefix match, not just a prefix: requires the '=' right after it, so "FOO" doesn't match "FOOBAR=x".
bool entry_matches_name(const std::string& entry, const char* name, size_t name_len) {
    return entry.size() > name_len && entry[name_len] == '=' && entry.compare(0, name_len, name) == 0;
}

// Finds the calling app instance's environment under the ledger lock.
// On a non-NULL return, the lock is left held for the caller to keep mutating/reading it,
// and must be released via mutex_unlock(&app_ledger().mutex) before returning to app_env_*()'s own caller.
AppEnv* lock_current_env() {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_scheduler_current_app_id());
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return nullptr;
    }
    return &iterator->second.env;
}

AppEnv::iterator find_entry(AppEnv& env, const char* name, size_t name_len) {
    return std::find_if(env.begin(), env.end(), [&](const std::string& entry) {
        return entry_matches_name(entry, name, name_len);
    });
}

} // namespace

void app_env_apply(AppEnv& env, const char* const* overlay) {
    for (int i = 0; overlay != nullptr && overlay[i] != nullptr; i++) {
        const char* separator = strchr(overlay[i], '=');
        if (separator == nullptr || separator == overlay[i]) {
            continue; // matches app_env_put()'s own rejection of a nameless/equals-less entry
        }
        const size_t name_len = static_cast<size_t>(separator - overlay[i]);
        const std::string name(overlay[i], name_len);
        auto iterator = find_entry(env, name.c_str(), name_len);
        if (iterator == env.end()) {
            env.emplace_back(overlay[i]);
        } else {
            *iterator = overlay[i];
        }
    }
}

extern "C" {

error_t app_env_set(const char* name, const char* value, bool overwrite) {
    if (!is_valid_name(name) || value == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    AppEnv* env = lock_current_env();
    if (env == nullptr) {
        return ERROR_NOT_FOUND;
    }

    const size_t name_len = strlen(name);
    auto iterator = find_entry(*env, name, name_len);
    if (iterator == env->end()) {
        env->emplace_back(std::string(name) + "=" + value);
    } else if (overwrite) {
        *iterator = std::string(name) + "=" + value;
    }
    mutex_unlock(&app_ledger().mutex);
    return ERROR_NONE;
}

error_t app_env_unset(const char* name) {
    if (!is_valid_name(name)) {
        return ERROR_INVALID_ARGUMENT;
    }

    AppEnv* env = lock_current_env();
    if (env == nullptr) {
        return ERROR_NOT_FOUND;
    }

    auto iterator = find_entry(*env, name, strlen(name));
    if (iterator != env->end()) {
        env->erase(iterator);
    }
    mutex_unlock(&app_ledger().mutex);
    return ERROR_NONE;
}

const char* app_env_get(const char* name) {
    if (!is_valid_name(name)) {
        return nullptr;
    }

    AppEnv* env = lock_current_env();
    if (env == nullptr) {
        return nullptr;
    }

    const size_t name_len = strlen(name);
    auto iterator = find_entry(*env, name, name_len);
    const char* result = iterator != env->end() ? iterator->c_str() + name_len + 1 : nullptr;
    mutex_unlock(&app_ledger().mutex);
    return result;
}

error_t app_env_put(const char* string) {
    if (string == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    const char* separator = strchr(string, '=');
    if (separator == nullptr || separator == string) {
        return ERROR_INVALID_ARGUMENT;
    }
    const std::string name(string, separator - string);
    return app_env_set(name.c_str(), separator + 1, true);
}

} // extern "C"
