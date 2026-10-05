// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

struct FileHolder {
    FILE* file = nullptr;

    ~FileHolder() {
        if (file != nullptr) {
            fclose(file);
        }
    }
};

// Destroyed when the binary is unloaded, after the app ended
std::vector<char> buffer;
FileHolder holder;

} // namespace

// Fills static objects with memory and a file their destructors release. Ends with exit() when argv[1] is "exit".
extern "C" int32_t main(int argc, char* argv[]) {
    buffer.resize(4096);
    holder.file = fopen("/dev/null", "r");
    if (holder.file == nullptr) {
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "exit") == 0) {
        exit(0);
    }
    return 0;
}
