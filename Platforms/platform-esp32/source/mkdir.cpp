// SPDX-License-Identifier: Apache-2.0
#include <sys/stat.h>

#include <cerrno>

// mkdir() on an existing FATFS mount root (e.g. "/sdcard") fails without setting EEXIST,
// which breaks the common "mkdir() then accept EEXIST" pattern. Any existing path reports EEXIST.
// A NULL path fails with EFAULT instead of crashing in the VFS (see vfs_null_path.cpp).
extern "C" {

int __real_mkdir(const char* path, mode_t mode);

int __wrap_mkdir(const char* path, mode_t mode) {
    if (path == nullptr) {
        errno = EFAULT;
        return -1;
    }
    struct stat info;
    if (stat(path, &info) == 0) {
        errno = EEXIST;
        return -1;
    }
    return __real_mkdir(path, mode);
}

}
