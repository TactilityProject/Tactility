#include "doctest.h"
#include <tactility/bundle.h>

#include <cstring>

TEST_CASE("bundle_alloc/bundle_free round-trip") {
    Bundle* bundle = bundle_alloc();
    CHECK_NE(bundle, nullptr);
    bundle_free(bundle);
}

TEST_CASE("bool can be stored and retrieved") {
    Bundle* bundle = bundle_alloc();
    bundle_put_bool(bundle, "key", true);

    CHECK(bundle_has_bool(bundle, "key"));
    CHECK_EQ(bundle_get_bool(bundle, "key"), true);

    bool out = false;
    CHECK(bundle_opt_bool(bundle, "key", &out));
    CHECK_EQ(out, true);

    bundle_free(bundle);
}

TEST_CASE("int32 can be stored and retrieved") {
    Bundle* bundle = bundle_alloc();
    bundle_put_int32(bundle, "key", -42);

    CHECK(bundle_has_int32(bundle, "key"));
    CHECK_EQ(bundle_get_int32(bundle, "key"), -42);

    int32_t out = 0;
    CHECK(bundle_opt_int32(bundle, "key", &out));
    CHECK_EQ(out, -42);

    bundle_free(bundle);
}

TEST_CASE("int64 can be stored and retrieved") {
    Bundle* bundle = bundle_alloc();
    bundle_put_int64(bundle, "key", 123456789012345LL);

    CHECK(bundle_has_int64(bundle, "key"));
    CHECK_EQ(bundle_get_int64(bundle, "key"), 123456789012345LL);

    int64_t out = 0;
    CHECK(bundle_opt_int64(bundle, "key", &out));
    CHECK_EQ(out, 123456789012345LL);

    bundle_free(bundle);
}

TEST_CASE("string can be stored and retrieved") {
    Bundle* bundle = bundle_alloc();
    bundle_put_string(bundle, "key", "hello world");

    CHECK(bundle_has_string(bundle, "key"));

    char buffer[32];
    CHECK_EQ(bundle_get_string(bundle, "key", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "hello world"), 0);

    char tiny[4];
    CHECK_EQ(bundle_get_string(bundle, "key", tiny, sizeof(tiny)), ERROR_BUFFER_OVERFLOW);

    char out[32];
    CHECK_EQ(bundle_opt_string(bundle, "key", out, sizeof(out)), ERROR_NONE);
    CHECK_EQ(std::strcmp(out, "hello world"), 0);

    bundle_free(bundle);
}

TEST_CASE("has_*/opt_* reject a key stored with a different type") {
    Bundle* bundle = bundle_alloc();
    bundle_put_bool(bundle, "key", true);

    CHECK_FALSE(bundle_has_int32(bundle, "key"));
    CHECK_FALSE(bundle_has_int64(bundle, "key"));
    CHECK_FALSE(bundle_has_string(bundle, "key"));

    int32_t out_int32 = 0;
    CHECK_FALSE(bundle_opt_int32(bundle, "key", &out_int32));

    char out_string[8];
    CHECK_EQ(bundle_opt_string(bundle, "key", out_string, sizeof(out_string)), ERROR_NOT_FOUND);

    bundle_free(bundle);
}

TEST_CASE("opt_string reports ERROR_NOT_FOUND for a missing key") {
    Bundle* bundle = bundle_alloc();
    char out[8];
    CHECK_EQ(bundle_opt_string(bundle, "missing", out, sizeof(out)), ERROR_NOT_FOUND);
    bundle_free(bundle);
}

TEST_CASE("bundle_clone makes an independent deep copy") {
    Bundle* original = bundle_alloc();
    bundle_put_bool(original, "bool", true);
    bundle_put_int32(original, "int32", 123);
    bundle_put_string(original, "string", "text");

    Bundle* clone = bundle_clone(original);
    bundle_free(original); // clone must not be affected

    CHECK_EQ(bundle_get_bool(clone, "bool"), true);
    CHECK_EQ(bundle_get_int32(clone, "int32"), 123);

    char buffer[16];
    CHECK_EQ(bundle_get_string(clone, "string", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "text"), 0);

    // Mutating the clone must not affect a re-clone of the (already-freed) original's data.
    bundle_put_int32(clone, "int32", 456);
    CHECK_EQ(bundle_get_int32(clone, "int32"), 456);

    bundle_free(clone);
}

TEST_CASE("put overwrites a previously stored value, including across types") {
    Bundle* bundle = bundle_alloc();
    bundle_put_int32(bundle, "key", 1);
    bundle_put_string(bundle, "key", "now a string");

    CHECK_FALSE(bundle_has_int32(bundle, "key"));
    CHECK(bundle_has_string(bundle, "key"));

    char buffer[32];
    CHECK_EQ(bundle_get_string(bundle, "key", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "now a string"), 0);

    bundle_free(bundle);
}
