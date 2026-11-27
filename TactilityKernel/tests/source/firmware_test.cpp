#include "doctest.h"

#include <tactility/firmware/firmware.h>


static const FirmwareOps test_ops {};

TEST_CASE("firmware_register makes a firmware available by id") {
    int context = 0;
    CHECK_EQ(firmware_register("test", &test_ops, &context), ERROR_NONE);

    const FirmwareOps* ops = nullptr;
    void* ctx = nullptr;
    CHECK_EQ(firmware_get("test", &ops, &ctx), ERROR_NONE);
    CHECK_EQ(ops, &test_ops);
    CHECK_EQ(ctx, &context);

    CHECK_EQ(firmware_register("test", &test_ops, nullptr), ERROR_ALREADY_EXISTS);
    CHECK_EQ(firmware_unregister("test"), ERROR_NONE);
    CHECK_EQ(firmware_get("test", &ops, &ctx), ERROR_NOT_FOUND);
    CHECK_EQ(firmware_unregister("test"), ERROR_NOT_FOUND);
}

TEST_CASE("firmware_for_each visits every registered firmware") {
    const char* ids[] = { "a", "b", "c", "d", "e" };
    for (const char* id : ids) {
        CHECK_EQ(firmware_register(id, &test_ops, nullptr), ERROR_NONE);
    }

    int count = 0;
    firmware_for_each([](const char*, const FirmwareOps*, void*, void* user_data) {
        (*static_cast<int*>(user_data))++;
        return true;
    }, &count);
    CHECK_EQ(count, 5);

    for (const char* id : ids) {
        CHECK_EQ(firmware_unregister(id), ERROR_NONE);
    }
}
