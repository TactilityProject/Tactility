#include "doctest.h"
#include <Tactility/kernel/Kernel.h>
#include <Tactility/Mutex.h>
#include <Tactility/Thread.h>

using namespace tt;

TEST_CASE("a Mutex can block a thread") {
    auto mutex = Mutex();
    CHECK_EQ(mutex.lock(kernel::MAX_TICKS), true);

    Thread thread = Thread(
        "thread",
        1024,
        [&mutex] {
            mutex.lock(kernel::MAX_TICKS);
            return 0;
        }
    );
    thread.start();

    kernel::delayMillis(5);
    CHECK_EQ(thread.getState(), Thread::State::Running);

    mutex.unlock();

    kernel::delayMillis(5);
    CHECK_EQ(thread.getState(), Thread::State::Stopped);

    thread.join();
}

TEST_CASE("a Mutex can be locked exactly once") {
    Mutex mutex;
    CHECK_EQ(mutex.lock(0), true);
    CHECK_EQ(mutex.lock(0), false);
    mutex.unlock();
}
