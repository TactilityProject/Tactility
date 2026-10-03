// SPDX-License-Identifier: Apache-2.0

// Symbols resolved for ELF apps ahead of every other module's (see app_esp32_symbol_resolver()).
// Firmware code calling the same functions never gets here.
// Pending signals are delivered here, at the entry and again when a call was interrupted by one,
// since the app holds no lock of the system itself there.
// Allocations are counted per app instance (see app/memory.h).
#include <app/libc.h>
#include <app/memory.h>
#include <app/signal.h>

#include <esp_heap_caps.h>

#include <tactility/module.h>

#include <sys/poll.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

ssize_t app_read(int fd, void* buffer, size_t size) {
    app_signal_deliver_pending();
    const ssize_t result = read(fd, buffer, size);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

ssize_t app_write(int fd, const void* buffer, size_t size) {
    app_signal_deliver_pending();
    const ssize_t result = write(fd, buffer, size);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

int app_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    app_signal_deliver_pending();
    const int result = poll(fds, nfds, timeout);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

int app_usleep(useconds_t usec) {
    app_signal_deliver_pending();
    int result;
    if (!app_libc_try_usleep(usec, &result)) {
        return usleep(usec);
    }
    app_signal_deliver_pending();
    return result;
}

unsigned int app_sleep(unsigned int seconds) {
    app_signal_deliver_pending();
    unsigned int result;
    if (!app_libc_try_sleep(seconds, &result)) {
        return sleep(seconds);
    }
    app_signal_deliver_pending();
    return result;
}

int app_kill(pid_t pid, int sig) {
    const int result = kill(pid, sig);
    // A signal sent to the app itself is delivered before kill() returns
    app_signal_deliver_pending();
    return result;
}

pid_t app_getpid() {
    int result;
    return app_libc_try_getpid(&result) ? result : 0;
}

pid_t app_getppid() {
    int result;
    return app_libc_try_getppid(&result) ? result : 0;
}

void* record_alloc(void* ptr) {
    if (ptr != nullptr) {
        app_memory_record_alloc(heap_caps_get_allocated_size(ptr));
    }
    return ptr;
}

void record_free(void* ptr) {
    if (ptr != nullptr) {
        app_memory_record_free(heap_caps_get_allocated_size(ptr));
    }
}

void* app_malloc(size_t size) {
    return record_alloc(malloc(size));
}

void* app_calloc(size_t count, size_t size) {
    return record_alloc(calloc(count, size));
}

void* app_realloc(void* ptr, size_t size) {
    const size_t old_size = (ptr != nullptr) ? heap_caps_get_allocated_size(ptr) : 0;
    void* result = realloc(ptr, size);
    // A failed realloc() leaves the old block allocated
    if (result != nullptr || size == 0) {
        if (ptr != nullptr) {
            app_memory_record_free(old_size);
        }
        record_alloc(result);
    }
    return result;
}

void app_free(void* ptr) {
    record_free(ptr);
    free(ptr);
}

char* app_strdup(const char* s) {
    return static_cast<char*>(record_alloc(strdup(s)));
}

char* app_strndup(const char* s, size_t n) {
    return static_cast<char*>(record_alloc(strndup(s, n)));
}

void* app_operator_new(size_t size) {
    return record_alloc(::operator new(size));
}

void* app_operator_new_array(size_t size) {
    return record_alloc(::operator new[](size));
}

void app_operator_delete(void* ptr) {
    record_free(ptr);
    ::operator delete(ptr);
}

void app_operator_delete_array(void* ptr) {
    record_free(ptr);
    ::operator delete[](ptr);
}

void app_operator_delete_sized(void* ptr, size_t) {
    app_operator_delete(ptr);
}

void app_operator_delete_array_sized(void* ptr, size_t) {
    app_operator_delete_array(ptr);
}

} // namespace

extern "C" {

extern const ModuleSymbol app_esp32_symbols[] = {
    { "read", reinterpret_cast<void*>(app_read) },
    { "write", reinterpret_cast<void*>(app_write) },
    { "poll", reinterpret_cast<void*>(app_poll) },
    { "usleep", reinterpret_cast<void*>(app_usleep) },
    { "sleep", reinterpret_cast<void*>(app_sleep) },
    { "kill", reinterpret_cast<void*>(app_kill) },
    { "getpid", reinterpret_cast<void*>(app_getpid) },
    { "getppid", reinterpret_cast<void*>(app_getppid) },
    { "malloc", reinterpret_cast<void*>(app_malloc) },
    { "calloc", reinterpret_cast<void*>(app_calloc) },
    { "realloc", reinterpret_cast<void*>(app_realloc) },
    { "free", reinterpret_cast<void*>(app_free) },
    { "strdup", reinterpret_cast<void*>(app_strdup) },
    { "strndup", reinterpret_cast<void*>(app_strndup) },
    { "_Znwj", reinterpret_cast<void*>(app_operator_new) }, // operator new(unsigned int)
    { "_Znaj", reinterpret_cast<void*>(app_operator_new_array) }, // operator new[](unsigned int)
    { "_ZdlPv", reinterpret_cast<void*>(app_operator_delete) }, // operator delete(void*)
    { "_ZdaPv", reinterpret_cast<void*>(app_operator_delete_array) }, // operator delete[](void*)
    { "_ZdlPvj", reinterpret_cast<void*>(app_operator_delete_sized) }, // operator delete(void*, unsigned int)
    { "_ZdaPvj", reinterpret_cast<void*>(app_operator_delete_array_sized) }, // operator delete[](void*, unsigned int)
    MODULE_SYMBOL_TERMINATOR
};

}
