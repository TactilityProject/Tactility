#include "doctest.h"

#include <atomic>

#include <tactility/concurrent/timer.h>
#include <tactility/delay.h>

TEST_CASE("timer_alloc and timer_free should handle allocation and deallocation") {
    auto callback = [](void* context) {};
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 10, callback, nullptr);
    CHECK_NE(timer, nullptr);
    timer_free(timer);
}

TEST_CASE("timer_start and timer_stop should change running state") {
    auto callback = [](void* context) {};
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 10, callback, nullptr);
    REQUIRE_NE(timer, nullptr);

    CHECK_EQ(timer_is_running(timer), false);
    CHECK_EQ(timer_start(timer), ERROR_NONE);
    CHECK_EQ(timer_is_running(timer), true);
    CHECK_EQ(timer_stop(timer), ERROR_NONE);
    CHECK_EQ(timer_is_running(timer), false);

    timer_free(timer);
}

TEST_CASE("one-shot timer should fire callback once") {
    std::atomic<int> call_count{0};
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 10, [](void* context) {
        auto* count = static_cast<std::atomic<int>*>(context);
        (*count)++;
    }, &call_count);
    REQUIRE_NE(timer, nullptr);

    CHECK_EQ(timer_start(timer), ERROR_NONE);
    delay_millis(20);
    
    CHECK_EQ(call_count.load(), 1);
    CHECK_EQ(timer_is_running(timer), false);

    timer_free(timer);
}

TEST_CASE("periodic timer should fire callback multiple times") {
    std::atomic<int> call_count{0};
    struct Timer* timer = timer_alloc(TIMER_TYPE_PERIODIC, 10, [](void* context) {
        auto* count = static_cast<std::atomic<int>*>(context);
        (*count)++;
    }, &call_count);
    REQUIRE_NE(timer, nullptr);

    CHECK_EQ(timer_start(timer), ERROR_NONE);
    delay_millis(35); // Should fire around 3 times
    
    CHECK_GE(call_count.load(), 3);
    CHECK_EQ(timer_is_running(timer), true);

    timer_stop(timer);
    timer_free(timer);
}

TEST_CASE("timer_reset should restart the timer") {
    std::atomic<int> call_count{0};
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 20, [](void* context) {
        auto* count = static_cast<std::atomic<int>*>(context);
        (*count)++;
    }, &call_count);
    REQUIRE_NE(timer, nullptr);

    CHECK_EQ(timer_start(timer), ERROR_NONE);
    delay_millis(10);
    CHECK_EQ(call_count.load(), 0);
    
    // Resetting should push the expiry further
    CHECK_EQ(timer_reset(timer), ERROR_NONE);
    delay_millis(15);
    CHECK_EQ(call_count.load(), 0); // Still shouldn't have fired if reset worked
    
    delay_millis(10);
    CHECK_EQ(call_count.load(), 1); // Now it should have fired

    timer_free(timer);
}

TEST_CASE("timer_reset_with_interval should change the period") {
    std::atomic<int> call_count{0};
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 40, [](void* context) {
        auto* count = static_cast<std::atomic<int>*>(context);
        (*count)++;
    }, &call_count);
    REQUIRE_NE(timer, nullptr);

    CHECK_EQ(timer_start(timer), ERROR_NONE);
    // Change to a much shorter interval
    CHECK_EQ(timer_reset_with_interval(timer, 10), ERROR_NONE);
    
    delay_millis(20);
    CHECK_EQ(call_count.load(), 1);

    timer_free(timer);
}

TEST_CASE("timer_get_expiry_time should return a valid time") {
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 10, [](void* context) {}, nullptr);
    REQUIRE_NE(timer, nullptr);

    timer_start(timer);
    TickType_t expiry = timer_get_expiry_time(timer);
    // Expiry should be in the future
    CHECK_GT(expiry, xTaskGetTickCount());

    timer_free(timer);
}

TEST_CASE("timer_set_pending_callback should execute callback in timer task") {
    std::atomic<bool> called{false};
    struct Context {
        std::atomic<bool>* called;
        uint32_t expected_arg;
        uint32_t received_arg;
    } context = { &called, 0x12345678, 0 };

    auto pending_cb = [](void* ctx, uint32_t arg) {
        auto* c = static_cast<Context*>(ctx);
        c->received_arg = arg;
        c->called->store(true);
    };

    // timer_set_pending_callback doesn't actually use the timer object in current implementation
    // but we need one for the API
    struct Timer* timer = timer_alloc(TIMER_TYPE_ONCE, 10, [](void* context) {}, nullptr);
    
    CHECK_EQ(timer_set_pending_callback(timer, pending_cb, &context, context.expected_arg, portMAX_DELAY), ERROR_NONE);
    
    // Wait for timer task to process the callback
    int retries = 10;
    while (!called.load() && retries-- > 0) {
        delay_millis(10);
    }

    CHECK(called.load());
    CHECK_EQ(context.received_arg, context.expected_arg);

    timer_free(timer);
}
