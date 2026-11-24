#include "doctest.h"

#include "../../Source/app/meshchat/SelectionState.h"

using tt::app::meshchat::SelectionState;

TEST_CASE("completed MeshChat selection keeps its kind until its result is consumed") {
    SelectionState state;
    REQUIRE(state.begin(SelectionState::Kind::AddType));
    REQUIRE(state.complete(1));

    CHECK_FALSE(state.begin(SelectionState::Kind::Menu));
    CHECK(state.isPending());

    SelectionState::Result result {};
    REQUIRE(state.takeResult(result));
    CHECK_EQ(result.kind, SelectionState::Kind::AddType);
    CHECK_EQ(result.selected, 1);
    CHECK_FALSE(state.isPending());

    REQUIRE(state.begin(SelectionState::Kind::Menu));
}

TEST_CASE("dismissed and failed MeshChat selections release the selection slot") {
    SelectionState state;
    REQUIRE(state.begin(SelectionState::Kind::Menu));
    REQUIRE(state.complete(-1));

    SelectionState::Result result {};
    REQUIRE(state.takeResult(result));
    CHECK_EQ(result.kind, SelectionState::Kind::Menu);
    CHECK_EQ(result.selected, -1);

    REQUIRE(state.begin(SelectionState::Kind::AddContact));
    state.failStart();
    CHECK_FALSE(state.isPending());
    REQUIRE(state.begin(SelectionState::Kind::AddType));
}