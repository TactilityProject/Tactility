#include "doctest.h"

#include "../../Source/app/addgps/SelectionState.h"

using tt::app::addgps::SelectionState;

TEST_CASE("completed GPS selection keeps its kind until its result is consumed") {
    SelectionState state;
    state.request(SelectionState::Kind::Uart);

    SelectionState::Kind kind;
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Uart);
    REQUIRE(state.complete(2));

    state.request(SelectionState::Kind::Model);
    CHECK_FALSE(state.beginNext(kind));

    SelectionState::Result result {};
    REQUIRE(state.takeResult(result));
    CHECK_EQ(result.kind, SelectionState::Kind::Uart);
    CHECK_EQ(result.selected, 2);

    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Model);
}

TEST_CASE("dismissals and failed dialog starts release the selection slot") {
    SelectionState state;
    state.request(SelectionState::Kind::Baud);

    SelectionState::Kind kind;
    REQUIRE(state.beginNext(kind));
    REQUIRE(state.complete(-1));

    SelectionState::Result result {};
    REQUIRE(state.takeResult(result));
    CHECK_EQ(result.kind, SelectionState::Kind::Baud);
    CHECK_EQ(result.selected, -1);

    state.request(SelectionState::Kind::Model);
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Model);
    state.failStart();
    state.request(SelectionState::Kind::Model);
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Model);
    state.failStart();
    state.request(SelectionState::Kind::Uart);
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Uart);
}

TEST_CASE("selection requests received while a dialog is active are retained") {
    SelectionState state;
    state.request(SelectionState::Kind::Uart);
    state.request(SelectionState::Kind::Model);
    state.request(SelectionState::Kind::Baud);

    SelectionState::Kind kind;
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Uart);
    REQUIRE(state.complete(0));

    state.request(SelectionState::Kind::Model);
    CHECK_FALSE(state.beginNext(kind));

    SelectionState::Result result {};
    REQUIRE(state.takeResult(result));
    CHECK_EQ(result.kind, SelectionState::Kind::Uart);
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Model);
    REQUIRE(state.complete(1));
    REQUIRE(state.takeResult(result));
    CHECK_EQ(result.kind, SelectionState::Kind::Model);
    REQUIRE(state.beginNext(kind));
    CHECK_EQ(kind, SelectionState::Kind::Baud);
}