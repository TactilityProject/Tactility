// SPDX-License-Identifier: Apache-2.0
#ifndef __APPLE__

// Counts the allocations made by a dlopen()ed app's own code (see app/memory.h). A .so resolves
// malloc through the dynamic linker, so these replace the process-wide functions and forward to
// glibc's own allocator. A call is counted only when its caller lies inside the image of the app
// running on the calling thread.
#include <app_posix/malloc_wrap.h>

#include <app/memory.h>

#include <malloc.h>

#include <cstdlib>
#include <cstring>
#include <new>

extern "C" {
void* __libc_malloc(size_t size);
void* __libc_calloc(size_t count, size_t size);
void* __libc_realloc(void* ptr, size_t size);
void __libc_free(void* ptr);
}

namespace {

// initial-exec, since the default TLS model of position independent code can call malloc itself
__attribute__((tls_model("initial-exec"))) thread_local uintptr_t image_start = 0;
__attribute__((tls_model("initial-exec"))) thread_local uintptr_t image_end = 0;

inline bool is_app_caller(void* caller) {
    const auto address = reinterpret_cast<uintptr_t>(caller);
    return address >= image_start && address < image_end;
}

inline void* record_alloc(void* ptr, void* caller) {
    if (ptr != nullptr && is_app_caller(caller)) {
        app_memory_record_alloc(malloc_usable_size(ptr));
    }
    return ptr;
}

inline void record_free(void* ptr, void* caller) {
    if (ptr != nullptr && is_app_caller(caller)) {
        app_memory_record_free(malloc_usable_size(ptr));
    }
}

void* new_or_throw(size_t size) {
    while (true) {
        void* ptr = __libc_malloc(size);
        if (ptr != nullptr) {
            return ptr;
        }
        std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            // Required by operator new's contract, so callers in libraries behave as before
#if __cpp_exceptions
            throw std::bad_alloc();
#else
            abort();
#endif
        }
        handler();
    }
}

} // namespace

void app_posix_set_current_image(uintptr_t start, uintptr_t end) {
    image_start = start;
    image_end = end;
}

extern "C" {

void* malloc(size_t size) {
    return record_alloc(__libc_malloc(size), __builtin_return_address(0));
}

void* calloc(size_t count, size_t size) {
    return record_alloc(__libc_calloc(count, size), __builtin_return_address(0));
}

void* realloc(void* ptr, size_t size) {
    void* caller = __builtin_return_address(0);
    if (!is_app_caller(caller)) {
        return __libc_realloc(ptr, size);
    }
    const size_t old_size = (ptr != nullptr) ? malloc_usable_size(ptr) : 0;
    void* result = __libc_realloc(ptr, size);
    // A failed realloc() leaves the old block allocated
    if (result != nullptr || size == 0) {
        if (ptr != nullptr) {
            app_memory_record_free(old_size);
        }
        if (result != nullptr) {
            app_memory_record_alloc(malloc_usable_size(result));
        }
    }
    return result;
}

void free(void* ptr) {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

char* strdup(const char* s) {
    const size_t length = strlen(s) + 1;
    auto* copy = static_cast<char*>(__libc_malloc(length));
    if (copy != nullptr) {
        memcpy(copy, s, length);
    }
    return static_cast<char*>(record_alloc(copy, __builtin_return_address(0)));
}

char* strndup(const char* s, size_t n) {
    const size_t length = strnlen(s, n);
    auto* copy = static_cast<char*>(__libc_malloc(length + 1));
    if (copy != nullptr) {
        memcpy(copy, s, length);
        copy[length] = '\0';
    }
    return static_cast<char*>(record_alloc(copy, __builtin_return_address(0)));
}

} // extern "C"

void* operator new(size_t size) {
    return record_alloc(new_or_throw(size), __builtin_return_address(0));
}

void* operator new[](size_t size) {
    return record_alloc(new_or_throw(size), __builtin_return_address(0));
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return record_alloc(__libc_malloc(size), __builtin_return_address(0));
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return record_alloc(__libc_malloc(size), __builtin_return_address(0));
}

void operator delete(void* ptr) noexcept {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    record_free(ptr, __builtin_return_address(0));
    __libc_free(ptr);
}

#endif
