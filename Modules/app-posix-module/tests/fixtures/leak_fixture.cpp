// SPDX-License-Identifier: Apache-2.0
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void* run_forever(void*) {
    while (true) {
        sleep(1);
    }
    return nullptr;
}

} // namespace

// Leaks an fd, a FILE, a DIR and memory, and writes their fds to argv[1].
// With "thread" as argv[2], it also leaves a thread running.
extern "C" int32_t main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }
    const int fd = open("/dev/null", O_RDONLY);
    FILE* file = fopen("/dev/null", "r");
    DIR* dir = opendir("/");
    void* memory = malloc(64);
    if (fd < 0 || file == nullptr || dir == nullptr || memory == nullptr) {
        return 2;
    }
    if (argc > 2 && strcmp(argv[2], "thread") == 0) {
        pthread_t thread;
        if (pthread_create(&thread, nullptr, run_forever, nullptr) != 0) {
            return 3;
        }
    }
    FILE* output = fopen(argv[1], "w");
    if (output == nullptr) {
        return 4;
    }
    fprintf(output, "%d %d %d", fd, fileno(file), dirfd(dir));
    fclose(output);
    return 0;
}
