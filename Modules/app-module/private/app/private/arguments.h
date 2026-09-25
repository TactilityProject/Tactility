// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstring>
#include <new>

/**
 * Deep-copies @a argv (@a argc <= 0 => NULL, matching "no parameters"). Used internally by
 * app_scheduler_start(), which takes ownership of the result regardless of outcome (freed via
 * app_arguments_free() once the spawned task's run() returns, or immediately on a failure to
 * start it).
 * @return NULL if @a argc <= 0 (no parameters), or if allocation failed. For @a argc > 0,
 * these are the only cases that produce NULL, so a caller can tell them apart by its own
 * already-known @a argc: NULL back from a positive @a argc always means allocation failed.
 * All partial allocations are freed before returning NULL, so failure never leaks memory.
 */
inline char** app_arguments_copy(int argc, const char* const argv[]) {
    if (argc <= 0) {
        return nullptr;
    }

    auto* copy = new (std::nothrow) char*[argc + 1];
    if (copy == nullptr) {
        return nullptr;
    }

    int copied = 0;
    for (; copied < argc; copied++) {
        size_t length = strlen(argv[copied]);
        copy[copied] = new (std::nothrow) char[length + 1];
        if (copy[copied] == nullptr) {
            break;
        }
        memcpy(copy[copied], argv[copied], length + 1);
    }

    if (copied < argc) {
        for (int i = 0; i < copied; i++) {
            delete[] copy[i];
        }
        delete[] copy;
        return nullptr;
    }

    copy[argc] = nullptr;
    return copy;
}

/**
 * Frees a deep-copied argv previously built by app_arguments_copy(): each individually
 * heap-allocated string, then the array itself. Safe to call with count == 0 / values == nullptr
 * (no-op).
 */
inline void app_arguments_free(int count, char** values) {
    if (values == nullptr) {
        return;
    }
    for (int i = 0; i < count; i++) {
        delete[] values[i];
    }
    delete[] values;
}

/**
 * Counts entries in a NULL-terminated array, e.g. TaskContext::argv/env (see app_arguments_copy(),
 * which always NULL-terminates its result) or an AppStartContext's own argv/environment before a
 * caller-given count is known.
 */
inline int app_arguments_count_null_terminated(const char* const* values) {
    int count = 0;
    if (values != nullptr) {
        while (values[count] != nullptr) {
            count++;
        }
    }
    return count;
}

/**
 * Same as app_arguments_free(), for a deep copy whose count wasn't kept around separately (e.g.
 * TaskContext::env) - walks until the NULL terminator app_arguments_copy() always leaves in
 * place, instead of taking an explicit count. Safe to call with values == nullptr (no-op).
 */
inline void app_arguments_free_null_terminated(char** values) {
    if (values == nullptr) {
        return;
    }
    for (char** p = values; *p != nullptr; p++) {
        delete[] *p;
    }
    delete[] values;
}
