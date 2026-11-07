// SPDX-License-Identifier: Apache-2.0
#include <tactility/filesystem/directory.h>
#include <tactility/filesystem/path.h>

#include <tactility/paths.h>

#include <sys/stat.h>

#include <cstring>
#include <set>
#include <utility>
#include <string>
#include <vector>

// Bound on how deep path_tree_size() walks, which only stops a loop of links as it doesn't use the stack.
constexpr int MAX_WALK_DEPTH = 64;

namespace {

/**
 * Sums the sizes of the files below a directory without recursing, so the stack use doesn't depend on how deep the tree is.
 * Only one directory is open at a time, and the directories still to visit are kept on the heap.
 */
uint64_t path_tree_size_at(const char* path) {
    if (!directory_exists(path)) {
        struct stat info;
        return (stat(path, &info) == 0) ? static_cast<uint64_t>(info.st_size) : 0;
    }

    struct Pending {
        std::string path;
        int depth;
    };
    struct Walk {
        std::vector<Pending> pending;
        // The directories that were queued, by device and inode, so links back to a directory don't repeat it
        std::set<std::pair<dev_t, ino_t>> visited;
        const Pending* current;
        uint64_t bytes;
    };
    Walk walk { {}, {}, nullptr, 0 };
    walk.pending.push_back({ path, 0 });
    struct stat root_info;
    if (stat(path, &root_info) == 0 && root_info.st_ino != 0) {
        walk.visited.insert({ root_info.st_dev, root_info.st_ino });
    }

    while (!walk.pending.empty()) {
        const Pending directory = std::move(walk.pending.back());
        walk.pending.pop_back();
        walk.current = &directory;
        directory_list(directory.path.c_str(), &walk, [](const DirectoryEntry* entry, void* context) {
            auto* walk = static_cast<Walk*>(context);
            if (!entry->is_directory) {
                walk->bytes += entry->size;
                return;
            }
            // The depth is bounded, so a loop of links ends. Paths that can't be handled by the file system's limits are skipped.
            if (walk->current->depth < MAX_WALK_DEPTH && walk->current->path.size() + 1 + strlen(entry->name) < FILE_MAX_PATH_STRING_LENGTH) {
                std::string child_path = walk->current->path + "/" + entry->name;
                struct stat child_info;
                // A directory that was queued before is skipped. Without inode numbers, only the depth limit applies.
                if (stat(child_path.c_str(), &child_info) == 0 && child_info.st_ino != 0 &&
                    !walk->visited.insert({ child_info.st_dev, child_info.st_ino }).second) {
                    return;
                }
                walk->pending.push_back({ std::move(child_path), walk->current->depth + 1 });
            }
        });
    }
    return walk.bytes;
}

} // namespace

extern "C" {

bool path_exists(const char* path) {
    struct stat info;
    return stat(path, &info) == 0;
}

uint64_t path_tree_size(const char* path) {
    return path_tree_size_at(path);
}

} // extern "C"
