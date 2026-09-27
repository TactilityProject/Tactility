// SPDX-License-Identifier: Apache-2.0
#include <sys/types.h>
#include <unistd.h>

// ESP-IDF doesn't provide getuid(). Tactility has no user accounts, so everything runs as root.
extern "C" uid_t getuid() {
    return 0;
}
