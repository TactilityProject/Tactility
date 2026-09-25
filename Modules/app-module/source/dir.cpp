// SPDX-License-Identifier: Apache-2.0
#include <app/dir.h>
#include <app/private/ledger.h>
#include <app/scheduler.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/filesystem/fs.h>

#include <cstring>

extern "C" {

error_t app_dir_get_cwd(char* buf, size_t size) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_scheduler_current_app_id());
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    const std::string& cwd = iterator->second.cwd;
    error_t result = ERROR_NONE;
    if (cwd.size() + 1 > size) {
        result = ERROR_BUFFER_OVERFLOW;
    } else {
        memcpy(buf, cwd.c_str(), cwd.size() + 1);
    }
    mutex_unlock(&ledger.mutex);
    return result;
}

error_t app_dir_set_cwd(const char* absolute_path) {
    if (absolute_path == nullptr || absolute_path[0] != '/') {
        return ERROR_INVALID_ARGUMENT;
    }
    // "/" is the synthetic root (a listing of mount points, see ShellFs.h) rather than a real
    // directory, so directory_exists() would reject it even though it's a valid cwd.
    if (strcmp(absolute_path, "/") != 0 && !directory_exists(absolute_path)) {
        return ERROR_NOT_FOUND;
    }

    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_scheduler_current_app_id());
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    iterator->second.cwd = absolute_path;
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

} // extern "C"
