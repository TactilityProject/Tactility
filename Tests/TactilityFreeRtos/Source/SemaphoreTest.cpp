#include "doctest.h"
#include <Tactility/Semaphore.h>

using namespace tt;

// We want a distinct test for 1 item, because it creates the Semaphore differently
TEST_CASE("a Semaphore with max count of 1 can be acquired exactly once") {
    auto semaphore = Semaphore(1);
    CHECK_EQ(semaphore.acquire(0), true);
    CHECK_EQ(semaphore.getAvailable(), 0);
    CHECK_EQ(semaphore.acquire(0), false);
    CHECK_EQ(semaphore.release(), true);
    CHECK_EQ(semaphore.getAvailable(), 1);
}

TEST_CASE("a Semaphore with max count of 2 can be acquired exactly twice") {
    auto semaphore = Semaphore(2);
    CHECK_EQ(semaphore.acquire(0), true);
    CHECK_EQ(semaphore.getAvailable(), 1);
    CHECK_EQ(semaphore.acquire(0), true);
    CHECK_EQ(semaphore.getAvailable(), 0);
    CHECK_EQ(semaphore.acquire(0), false);
    CHECK_EQ(semaphore.release(), true);
    CHECK_EQ(semaphore.getAvailable(), 1);
    CHECK_EQ(semaphore.release(), true);
    CHECK_EQ(semaphore.getAvailable(), 2);
}

TEST_CASE("the semaphore count should be correct initially") {
    auto semaphore_a = Semaphore(2);
    CHECK_EQ(semaphore_a.getAvailable(), 2);

    auto semaphore_b = Semaphore(2, 0);
    CHECK_EQ(semaphore_b.getAvailable(), 0);
}
