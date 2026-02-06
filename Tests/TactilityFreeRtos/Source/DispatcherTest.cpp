#include "doctest.h"
#include <Tactility/Dispatcher.h>

using namespace tt;

TEST_CASE("dispatcher should not call callback if consume isn't called") {
    int counter = 0;
    Dispatcher dispatcher;
    dispatcher.dispatch([&counter]() { counter++; });
    kernel::delayTicks(10);

    CHECK_EQ(counter, 0);
}

TEST_CASE("dispatcher should be able to dealloc when message is not consumed") {
    auto* dispatcher = new Dispatcher();
    auto context = std::make_shared<uint32_t>();
    dispatcher->dispatch([]() { /* NO-OP */ });
    delete dispatcher;
}

TEST_CASE("dispatcher should call callback when consume is called") {
    int counter = 0;
    Dispatcher dispatcher;

    CHECK_EQ(dispatcher.dispatch([&counter] { counter++; }), true);
    CHECK_EQ(dispatcher.consume(100), 1);

    CHECK_EQ(counter, 1);
}

TEST_CASE("message should be passed on correctly") {
    Dispatcher dispatcher;

    dispatcher.dispatch([]() { /* NO-OP */ });
    dispatcher.consume(100);
}
