// SPDX-License-Identifier: Apache-2.0
#include <app/libc.h>

#include <tactility/paths.h>

#include <reent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <cerrno>

// Wraps of the path-based VFS calls (-Wl,--wrap=, see the top-level CMakeLists.txt):
// - ESP-IDF's VFS has no cwd: an app instance's relative paths are resolved against its own (see app/libc.h).
// - ESP-IDF's VFS dereferences a NULL path. These fail with EFAULT instead, like other POSIX systems do.
// opendir() gets the same treatment in its own wrap (root_dir.cpp).

namespace {

/**
 * @return the path to pass on to the VFS: resolved for an app instance, as-is otherwise.
 * NULL when the resolved path doesn't fit (errno is set to ENAMETOOLONG).
 */
const char* resolve(const char* path, char (&buffer)[FILE_MAX_PATH_STRING_LENGTH]) {
    const char* resolved;
    return app_libc_try_resolve_path(path, buffer, sizeof(buffer), &resolved) ? resolved : path;
}

} // namespace

extern "C" {

int __real__open_r(struct _reent* r, const char* path, int flags, int mode);
int __real__stat_r(struct _reent* r, const char* path, struct stat* st);
int __real__link_r(struct _reent* r, const char* n1, const char* n2);
int __real__unlink_r(struct _reent* r, const char* path);
int __real__rename_r(struct _reent* r, const char* src, const char* dst);
int __real_truncate(const char* path, off_t length);
int __real_access(const char* path, int amode);
int __real_utime(const char* path, const struct utimbuf* times);
int __real_rmdir(const char* name);
int __real_mkdir(const char* path, mode_t mode);

// open() and fopen() both go through _open_r
int __wrap__open_r(struct _reent* r, const char* path, int flags, int mode) {
    if (path == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    return __real__open_r(r, resolved, flags, mode);
}

int __wrap__stat_r(struct _reent* r, const char* path, struct stat* st) {
    if (path == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    return __real__stat_r(r, resolved, st);
}

int __wrap__link_r(struct _reent* r, const char* n1, const char* n2) {
    if (n1 == nullptr || n2 == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    char buffer1[FILE_MAX_PATH_STRING_LENGTH];
    char buffer2[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved1 = resolve(n1, buffer1);
    const char* resolved2 = resolve(n2, buffer2);
    if (resolved1 == nullptr || resolved2 == nullptr) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    return __real__link_r(r, resolved1, resolved2);
}

// unlink() and remove() both go through _unlink_r
int __wrap__unlink_r(struct _reent* r, const char* path) {
    if (path == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    return __real__unlink_r(r, resolved);
}

int __wrap__rename_r(struct _reent* r, const char* src, const char* dst) {
    if (src == nullptr || dst == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    char src_buffer[FILE_MAX_PATH_STRING_LENGTH];
    char dst_buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved_src = resolve(src, src_buffer);
    const char* resolved_dst = resolve(dst, dst_buffer);
    if (resolved_src == nullptr || resolved_dst == nullptr) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    return __real__rename_r(r, resolved_src, resolved_dst);
}

int __wrap_truncate(const char* path, off_t length) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_truncate(resolved, length);
}

int __wrap_access(const char* path, int amode) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_access(resolved, amode);
}

int __wrap_utime(const char* path, const struct utimbuf* times) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_utime(resolved, times);
}

int __wrap_rmdir(const char* name) {
    if (name == nullptr) {
        errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(name, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_rmdir(resolved);
}

// mkdir() on an existing FATFS mount root (e.g. "/sdcard") fails without setting EEXIST,
// which breaks the common "mkdir() then accept EEXIST" pattern. Any existing path reports EEXIST.
int __wrap_mkdir(const char* path, mode_t mode) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolve(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    struct stat info;
    if (stat(resolved, &info) == 0) {
        errno = EEXIST;
        return -1;
    }
    return __real_mkdir(resolved, mode);
}

}
