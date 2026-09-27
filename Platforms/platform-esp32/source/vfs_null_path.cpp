// SPDX-License-Identifier: Apache-2.0
#include <reent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <cerrno>

// ESP-IDF's VFS dereferences a NULL path. These fail with EFAULT instead, like other POSIX systems do.
// opendir() and mkdir() get the same check in their own wraps (root_dir.cpp, mkdir.cpp).
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

// open() and fopen() both go through _open_r
int __wrap__open_r(struct _reent* r, const char* path, int flags, int mode) {
    if (path == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    return __real__open_r(r, path, flags, mode);
}

int __wrap__stat_r(struct _reent* r, const char* path, struct stat* st) {
    if (path == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    return __real__stat_r(r, path, st);
}

int __wrap__link_r(struct _reent* r, const char* n1, const char* n2) {
    if (n1 == nullptr || n2 == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    return __real__link_r(r, n1, n2);
}

int __wrap__unlink_r(struct _reent* r, const char* path) {
    if (path == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    return __real__unlink_r(r, path);
}

int __wrap__rename_r(struct _reent* r, const char* src, const char* dst) {
    if (src == nullptr || dst == nullptr) {
        r->_errno = EFAULT;
        return -1;
    }
    return __real__rename_r(r, src, dst);
}

int __wrap_truncate(const char* path, off_t length) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    return __real_truncate(path, length);
}

int __wrap_access(const char* path, int amode) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    return __real_access(path, amode);
}

int __wrap_utime(const char* path, const struct utimbuf* times) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    return __real_utime(path, times);
}

int __wrap_rmdir(const char* name) {
    if (name == nullptr) {
        errno = EFAULT;
        return -1;
    }
    return __real_rmdir(name);
}

}
