#ifdef ESP_PLATFORM

// There is no real filesystem at "/" on ESP32 and esp_vfs_register() rejects "/" as a mount prefix.
// Every real mount lives at "/data", "/sdcard", etc.), so opendir("/")/readdir()/ closedir() are wrapped
// synthesize a listing of registered filesystems
//
// DIR's own layout (components/vfs's sys/dirent.h) is deliberately minimal:
// Just a `dd_vfs_idx` (which registered VFS owns this handle) and a reserved `dd_rsv`,
// with "remaining fields defined by VFS implementation".
// So a real DIR* and this file's own RootDir agree on the first 4 bytes regardless of who allocated it.
// `dd_vfs_idx` is never a real VFS's table offset here and `dd_rsv` is left untouched (0) by every real VFS implementation,
// so tagging it with a nonzero magic value reliably tells our handles apart from real ones.

#include <tactility/filesystem/file_system.h>

#include <dirent.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint16_t ROOT_DIR_VFS_IDX = 0xFFFF;
constexpr uint16_t ROOT_DIR_MAGIC = 0x5A11;
constexpr int ROOT_DIR_MAX_ENTRIES = 16;

struct RootDir {
    uint16_t dd_vfs_idx;
    uint16_t dd_rsv;
    int count;
    int position;
    char names[ROOT_DIR_MAX_ENTRIES][64];
    struct dirent entry;
};

bool isRootDir(DIR* pdir) {
    auto* dir = reinterpret_cast<RootDir*>(pdir);
    return dir->dd_vfs_idx == ROOT_DIR_VFS_IDX && dir->dd_rsv == ROOT_DIR_MAGIC;
}

// Copies the mount path's top-level segment rather than using file_system_get_name(): the
// registered name isn't always the mount point, and isn't guaranteed to be a string literal
// that outlives this snapshot.
bool collectMountName(FileSystem* fs, void* context) {
    auto* dir = static_cast<RootDir*>(context);
    if (dir->count >= ROOT_DIR_MAX_ENTRIES) {
        return false;
    }
    char path[64];
    if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE) {
        return true;
    }
    const char* top = (path[0] == '/') ? path + 1 : path;
    if (top[0] == '\0' || strchr(top, '/') != nullptr) {
        return true; // not a top-level mount point
    }
    snprintf(dir->names[dir->count], sizeof(dir->names[0]), "%s", top);
    dir->count++;
    return true;
}

} // namespace

extern "C" DIR* __real_opendir(const char* name);
extern "C" struct dirent* __real_readdir(DIR* pdir);
extern "C" int __real_closedir(DIR* pdir);

extern "C" {

DIR* __wrap_opendir(const char* name) {
    if (name == nullptr || strcmp(name, "/") != 0) {
        return __real_opendir(name);
    }

    auto* dir = static_cast<RootDir*>(calloc(1, sizeof(RootDir)));
    if (dir == nullptr) {
        return nullptr; // matches opendir()'s own ENOMEM contract; errno is left to the caller
    }
    dir->dd_vfs_idx = ROOT_DIR_VFS_IDX;
    dir->dd_rsv = ROOT_DIR_MAGIC;
    file_system_for_each_mounted(dir, collectMountName);
    return reinterpret_cast<DIR*>(dir);
}

struct dirent* __wrap_readdir(DIR* pdir) {
    if (pdir == nullptr || !isRootDir(pdir)) {
        return __real_readdir(pdir);
    }

    auto* dir = reinterpret_cast<RootDir*>(pdir);
    if (dir->position >= dir->count) {
        return nullptr; // end of directory; errno intentionally left unchanged
    }

    dir->entry.d_ino = 0;
    dir->entry.d_type = DT_DIR;
    snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s", dir->names[dir->position]);
    dir->position++;
    return &dir->entry;
}

int __wrap_closedir(DIR* pdir) {
    if (pdir == nullptr || !isRootDir(pdir)) {
        return __real_closedir(pdir);
    }
    free(pdir);
    return 0;
}

}

#endif // ESP_PLATFORM
