#include "doctest.h"
#include <Tactility/DispatcherThread.h>

using namespace tt;

TEST_CASE("DispatcherThread state test") {
    DispatcherThread thread("test");
    CHECK_EQ(thread.isStarted(), false);

    thread.start();
    CHECK_EQ(thread.isStarted(), true);

    thread.stop();
    CHECK_EQ(thread.isStarted(), false);
}

TEST_CASE("DispatcherThread should consume jobs") {
    DispatcherThread thread("test");
    thread.start();
    int counter = 0;

    thread.dispatch([&counter]() { counter++; });

    tt::kernel::delayTicks(10);

    CHECK_EQ(counter, 1);
    thread.stop();
}
