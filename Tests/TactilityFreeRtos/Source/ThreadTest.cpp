#include "doctest.h"
#include <Tactility/Thread.h>

using namespace tt;

TEST_CASE("when a thread is started then its callback should be called") {
    bool has_called = false;
    auto* thread = new Thread(
        "immediate return task",
        4096,
        [&has_called]() {
          has_called = true;
          return 0;
        }
    );

    CHECK(!has_called);
    thread->start();
    thread->join();
    delete thread;
    CHECK(has_called);
}

TEST_CASE("a thread can be started and stopped") {
    bool interrupted = false;
    auto* thread = new Thread(
        "interruptable thread",
        4096,
        [&interrupted]() {
            while (!interrupted) {
                kernel::delayMillis(5);
            }
            return 0;
        }
    );

    CHECK(thread);
    thread->start();
    interrupted = true;
    thread->join();
    delete thread;
}

TEST_CASE("thread id should only be set at when thread is started") {
    bool interrupted = false;
    auto* thread = new Thread(
        "interruptable thread",
        4096,
        [&interrupted]() {
          while (!interrupted) {
              kernel::delayMillis(5);
          }
          return 0;
        }
    );
    CHECK_EQ(thread->getTaskHandle(), nullptr);
    thread->start();
    CHECK_NE(thread->getTaskHandle(), nullptr);
    interrupted = true;
    thread->join();
    CHECK_EQ(thread->getTaskHandle(), nullptr);
    delete thread;
}

TEST_CASE("thread state should be correct") {
    bool interrupted = false;
    auto* thread = new Thread(
        "interruptable thread",
        4096,
        [&interrupted]() {
          while (!interrupted) {
              kernel::delayMillis(5);
          }
          return 0;
        }

    );
    CHECK_EQ(thread->getState(), Thread::State::Stopped);
    thread->start();
    Thread::State state = thread->getState();
    CHECK((state == Thread::State::Starting || state == Thread::State::Running));
    interrupted = true;
    thread->join();
    CHECK_EQ(thread->getState(), Thread::State::Stopped);
    delete thread;
}

TEST_CASE("thread id should only be set at when thread is started") {
    int code = 123;
    auto* thread = new Thread(
        "return code",
        4096,
        [&code]() { return code; }
    );
    thread->start();
    thread->join();
    CHECK_EQ(thread->getReturnCode(), code);
    delete thread;
}
