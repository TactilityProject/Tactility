#include "doctest.h"
#include <Tactility/Bundle.h>

using namespace tt;

TEST_CASE("boolean can be stored and retrieved") {
    Bundle bundle;
    bundle.putBool("key", true);
    CHECK(bundle.hasBool("key"));
    CHECK(bundle.getBool("key"));
    bool opt_result = false;
    CHECK(bundle.optBool("key", opt_result));
    CHECK_EQ(opt_result, true);
}

TEST_CASE("int32 can be stored and retrieved") {
    Bundle bundle;
    bundle.putInt32("key", true);
    CHECK(bundle.hasInt32("key"));
    CHECK(bundle.getInt32("key"));
    int32_t opt_result = false;
    CHECK(bundle.optInt32("key", opt_result));
    CHECK_EQ(opt_result, true);
}

TEST_CASE("string can be stored and retrieved") {
    Bundle bundle;
    bundle.putString("key", "test");
    CHECK(bundle.hasString("key"));
    CHECK_EQ(bundle.getString("key"), "test");
    std::string opt_result;
    CHECK(bundle.optString("key", opt_result));
    CHECK_EQ(opt_result, "test");
}

TEST_CASE("bundle copy makes an actual copy") {
    auto* original_ptr = new Bundle();
    Bundle& original = *original_ptr;
    original.putBool("bool", true);
    original.putInt32("int32", 123);
    original.putString("string", "text");

    Bundle copy = original;
    delete original_ptr;

    CHECK_EQ(copy.getBool("bool"), true);
    CHECK_EQ(copy.getInt32("int32"),  123);
    CHECK_EQ(copy.getString("string"), "text");
}
