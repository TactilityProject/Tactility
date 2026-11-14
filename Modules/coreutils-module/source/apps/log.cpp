#include <app/manifest.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <csignal>
#include <cstdio>
#include <poll.h>
#include <unistd.h>

namespace coreutils::log {

constexpr size_t RING_CAPACITY = 2048;
constexpr size_t CHUNK_SIZE = 512;
constexpr int POLL_INTERVAL_MS = 50;

/** Hands lines from the log drain task to the app's task. It only holds what wasn't printed yet. */
struct Ring {
    Mutex mutex;
    size_t head = 0;
    size_t count = 0;
    size_t dropped = 0;
    char data[RING_CAPACITY];
};

static volatile sig_atomic_t stopRequested = 0;

static void onSignal(int) {
    stopRequested = 1;
}

/** Whole lines only: a line that doesn't fit is dropped and counted */
static void onLogLine(const char* line, size_t length, void* context) {
    auto* ring = static_cast<Ring*>(context);
    mutex_lock(&ring->mutex);
    if (length > RING_CAPACITY - ring->count) {
        ring->dropped++;
    } else {
        size_t tail = (ring->head + ring->count) % RING_CAPACITY;
        for (size_t i = 0; i < length; i++) {
            ring->data[(tail + i) % RING_CAPACITY] = line[i];
        }
        ring->count += length;
    }
    mutex_unlock(&ring->mutex);
}

static void drain(Ring* ring) {
    char chunk[CHUNK_SIZE];
    while (true) {
        size_t length = 0;
        size_t dropped = 0;
        mutex_lock(&ring->mutex);
        length = ring->count < sizeof(chunk) ? ring->count : sizeof(chunk);
        for (size_t i = 0; i < length; i++) {
            chunk[i] = ring->data[(ring->head + i) % RING_CAPACITY];
        }
        ring->head = (ring->head + length) % RING_CAPACITY;
        ring->count -= length;
        dropped = ring->dropped;
        ring->dropped = 0;
        mutex_unlock(&ring->mutex);

        if (dropped > 0) {
            printf("[%zu log lines dropped]\n", dropped);
        }
        if (length == 0) {
            break;
        }
        fwrite(chunk, 1, length, stdout);
    }
    fflush(stdout);
}

/** Returns true when any key was typed or stdin was closed */
static bool waitForInput() {
    pollfd descriptor = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
    const int result = poll(&descriptor, 1, POLL_INTERVAL_MS);
    if (result <= 0) {
        return false;
    }
    if (descriptor.revents & (POLLHUP | POLLERR)) {
        return true;
    }
    char input[16];
    read(STDIN_FILENO, input, sizeof(input));
    return true;
}

static int32_t main(int, char*[]) {
    MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* ring = static_cast<Ring*>(memory_alloc_with_policy(sizeof(Ring), &policy));
    if (ring == nullptr) {
        fprintf(stderr, "log: out of memory\n");
        return 1;
    }
    ring->head = 0;
    ring->count = 0;
    ring->dropped = 0;
    mutex_construct(&ring->mutex);

    stopRequested = 0;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGHUP, onSignal);

    if (!log_subscribe(onLogLine, ring)) {
        fprintf(stderr, "log: too many log listeners\n");
        mutex_destruct(&ring->mutex);
        memory_free(ring);
        return 1;
    }

    while (!stopRequested) {
        if (waitForInput()) {
            break;
        }
        drain(ring);
    }

    log_unsubscribe(onLogLine, ring);
    mutex_destruct(&ring->mutex);
    memory_free(ring);
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "log",
    .name = "log",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::log
