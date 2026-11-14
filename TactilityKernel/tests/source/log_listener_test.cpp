#include <doctest.h>

#include <tactility/log.h>
#include <tactility/log_queue.h>

#include <tactility/freertos/task.h>

#include <atomic>
#include <cstring>
#include <string>

namespace {

struct Received {
    std::atomic<int> count { 0 };
    std::string last;
};

void onLine(const char* line, size_t length, void* context) {
    auto* received = static_cast<Received*>(context);
    received->last.assign(line, length);
    received->count++;
}

/** Removes the listener when a failing assertion ends the test early */
struct Subscription {
    Received* received;

    ~Subscription() { log_unsubscribe(onLine, received); }
};

bool waitForCount(Received& received, int expected) {
    for (int i = 0; i < 100 && received.count < expected; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return received.count >= expected;
}

}

TEST_CASE("log listener receives lines until unsubscribed") {
    log_queue_init();
    Received received;
    REQUIRE(log_subscribe(onLine, &received));
    Subscription subscription { &received };

    const char* line = "hello\n";
    log_queue_write(line, strlen(line));
    REQUIRE(waitForCount(received, 1));
    CHECK(received.last == "hello\n");

    log_unsubscribe(onLine, &received);
    const int countAfterUnsubscribe = received.count;
    log_queue_write(line, strlen(line));
    vTaskDelay(pdMS_TO_TICKS(100));
    CHECK(received.count == countAfterUnsubscribe);
}
