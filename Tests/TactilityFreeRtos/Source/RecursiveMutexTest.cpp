#include "doctest.h"
#include <Tactility/kernel/Kernel.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Thread.h>

using namespace tt;

TEST_CASE("a RecursiveMutex can block a thread") {
    auto mutex = RecursiveMutex();
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

TEST_CASE("a RecursiveMutex can be locked more than once from the same context") {
    RecursiveMutex mutex;
    CHECK_EQ(mutex.lock(0), true);
    CHECK_EQ(mutex.lock(0), true);
    mutex.unlock();
}
