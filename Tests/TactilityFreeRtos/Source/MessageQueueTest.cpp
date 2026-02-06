#include "doctest.h"
#include <Tactility/MessageQueue.h>

using namespace tt;

TEST_CASE("message queue initial count should be 0") {
    MessageQueue queue(10, 1);

    uint32_t count = queue.getCount();
    CHECK_EQ(count, 0);
}

TEST_CASE("message queue count should increase when message is added") {
    MessageQueue queue(10, sizeof(uint32_t));

    uint32_t message = 123;
    queue.put(&message, 100);
    uint32_t count = queue.getCount();
    CHECK_EQ(count, 1);
}

TEST_CASE("message queue count should be 0 when message is added and queue is reset") {
    MessageQueue queue(10, sizeof(uint32_t));

    uint32_t message = 123;

    queue.put(&message, 100);
    queue.reset();
    uint32_t count = queue.getCount();
    CHECK_EQ(count, 0);
}

TEST_CASE("message queue consumption should work") {
    MessageQueue queue(10, sizeof(uint32_t));

    uint32_t out_message = 123;
    queue.put(&out_message, 100);

    uint32_t in_message = 0;
    queue.get(&in_message, 100);
    CHECK_EQ(in_message, 123);
}

TEST_CASE("message queue count should decrease when message is consumed") {
    MessageQueue queue(10, sizeof(uint32_t));

    uint32_t out_message = 123;
    queue.put(&out_message, 100);

    uint32_t in_message = 0;
    queue.get(&in_message, 100);
    uint32_t count = queue.getCount();
    CHECK_EQ(count, 0);
}

TEST_CASE("message queue should make copy of data") {
    // Given a number that we can later delete
    MessageQueue queue(1, sizeof(int32_t));
    const int32_t test_value = 123;
    auto* number = new int32_t();
    *number = test_value;

    // When we put the number in the queue and then delete it
    queue.put(number, 100);
    delete number;

    // We want to verify that the value was copied into the queue and retrieved properly
    int32_t queue_number = 0;
    CHECK_EQ(queue.get(&queue_number, 100), true);
    CHECK_EQ(queue_number, test_value);
}
