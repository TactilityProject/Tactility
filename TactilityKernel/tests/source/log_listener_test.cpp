#include <doctest.h>

#include <tactility/log.h>
#include <tactility/log_queue.h>

#include <tactility/freertos/task.h>

#include <atomic>
#include <cstring>
#include <string>

namespace {

constexpr auto* TEST_LINE = "hello\n";

/** The queue is shared, so lines that other tests logged earlier can arrive too. Only the test line is counted. */
struct Received {
    std::atomic<int> count { 0 };
};

void onLine(const char* line, size_t length, void* context) {
    auto* received = static_cast<Received*>(context);
    if (std::string(line, length) == TEST_LINE) {
        received->count++;
    }
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

    log_queue_write(TEST_LINE, strlen(TEST_LINE));
    REQUIRE(waitForCount(received, 1));

    log_unsubscribe(onLine, &received);
    const int countAfterUnsubscribe = received.count;
    log_queue_write(TEST_LINE, strlen(TEST_LINE));
    vTaskDelay(pdMS_TO_TICKS(100));
    CHECK(received.count == countAfterUnsubscribe);
}
