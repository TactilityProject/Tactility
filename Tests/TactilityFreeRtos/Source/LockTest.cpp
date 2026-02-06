#include "doctest.h"
#include <Tactility/Semaphore.h>
#include <Tactility/Lock.h>
#include <Tactility/Mutex.h>

using namespace tt;

TEST_CASE("withLock() locks correctly on Semaphore") {
    auto semaphore = std::make_shared<Semaphore>(2U);
    semaphore->withLock([semaphore](){
        CHECK_EQ(semaphore->getAvailable(), 1);
    });
}

TEST_CASE("withLock() unlocks correctly on Semaphore") {
    auto semaphore = std::make_shared<Semaphore>(2U);
    semaphore->withLock([=](){
        // NO-OP
    });

    CHECK_EQ(semaphore->getAvailable(), 2);
}

TEST_CASE("withLock() locks correctly on Mutex") {
    auto mutex = std::make_shared<Mutex>();
    mutex->withLock([mutex](){
        CHECK_EQ(mutex->lock(1), false);
    });
}

TEST_CASE("withLock() unlocks correctly on Mutex") {
    auto mutex = std::make_shared<Mutex>();
    mutex->withLock([=](){
      // NO-OP
    });

    CHECK_EQ(mutex->lock(1), true);
    mutex->unlock();
}
