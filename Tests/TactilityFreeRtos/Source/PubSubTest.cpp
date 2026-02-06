#include "doctest.h"
#include <Tactility/PubSub.h>

using namespace tt;

TEST_CASE("PubSub publishing with no subscriptions should not crash") {
    PubSub<int> pubsub;
    pubsub.publish(1);
}

TEST_CASE("PubSub subscription receives published data") {
    PubSub<int> pubsub;
    int value = 0;

    auto subscription = pubsub.subscribe([&value](auto newValue) {
        value = newValue;
    });
    pubsub.publish(1);

    pubsub.unsubscribe(subscription);

    CHECK_EQ(value, 1);
}

TEST_CASE("PubSub unsubscribed subscription does not receive published data") {
    PubSub<int> pubsub;
    int value = 0;

    auto subscription = pubsub.subscribe([&value](auto newValue) {
        value = newValue;
    });
    pubsub.unsubscribe(subscription);
    pubsub.publish(1);

    CHECK_EQ(value, 0);
}
