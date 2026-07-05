#include "doctest.h"
#include <tactility/service/service_manager.h>

// Defined in service_instance.cpp. Internal-only, exposed here to test try_get/put gating.
extern "C" void service_instance_set_state(ServiceInstance* instance, ServiceState state);

static int create_called = 0;
static int destroy_called = 0;
static int on_start_called = 0;
static int on_stop_called = 0;
static error_t on_start_result = ERROR_NONE;
static void* last_create_context = nullptr;
static void* last_destroy_context = nullptr;

static void test_create_service(ServiceInstance* instance, void* context) {
    create_called++;
    last_create_context = context;
    instance->data = nullptr;
    instance->on_start = [](ServiceInstance*) -> error_t {
        on_start_called++;
        return on_start_result;
    };
    instance->on_stop = [](ServiceInstance*) {
        on_stop_called++;
    };
}

static void test_destroy_service(ServiceInstance*, void* context) {
    destroy_called++;
    last_destroy_context = context;
}

static void reset_counters() {
    create_called = 0;
    destroy_called = 0;
    on_start_called = 0;
    on_stop_called = 0;
    on_start_result = ERROR_NONE;
    last_create_context = nullptr;
    last_destroy_context = nullptr;
}

TEST_CASE("ServiceInstance construction and destruction") {
    reset_counters();

    static int context_marker = 0;
    static const ServiceManifest manifest = {
        .id = "instance-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service,
        .context = &context_marker
    };

    ServiceInstance instance = { .manifest = nullptr, .data = nullptr, .on_start = nullptr, .on_stop = nullptr, .internal = nullptr };

    CHECK_EQ(service_instance_construct(&instance, &manifest), ERROR_NONE);
    CHECK_NE(instance.internal, nullptr);
    CHECK_EQ(instance.manifest, &manifest);
    CHECK_NE(instance.on_start, nullptr);
    CHECK_NE(instance.on_stop, nullptr);
    CHECK_EQ(create_called, 1);
    CHECK_EQ(last_create_context, &context_marker);
    CHECK_EQ(service_instance_get_state(&instance), SERVICE_STATE_STOPPED);

    CHECK_EQ(service_instance_destruct(&instance), ERROR_NONE);
    CHECK_EQ(instance.internal, nullptr);
    CHECK_EQ(destroy_called, 1);
    CHECK_EQ(last_destroy_context, &context_marker);
}

TEST_CASE("service_instance_try_get/put reference counting") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "refcount-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    ServiceInstance instance = { .manifest = nullptr, .data = nullptr, .on_start = nullptr, .on_stop = nullptr, .internal = nullptr };
    CHECK_EQ(service_instance_construct(&instance, &manifest), ERROR_NONE);

    // Not started yet: claiming usage should fail
    CHECK_FALSE(service_instance_try_get(&instance));

    service_instance_set_state(&instance, SERVICE_STATE_STARTED);

    CHECK(service_instance_try_get(&instance));
    CHECK(service_instance_try_get(&instance));
    service_instance_put(&instance);
    service_instance_put(&instance);

    // Extra put beyond the claimed count should not underflow
    service_instance_put(&instance);

    service_instance_set_state(&instance, SERVICE_STATE_STOPPED);
    CHECK_EQ(service_instance_destruct(&instance), ERROR_NONE);
}

TEST_CASE("service_manager_add rejects duplicate ids") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "duplicate-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_manager_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_manager_add(&manifest, false), ERROR_INVALID_ARGUMENT);

    CHECK_EQ(service_manager_remove("duplicate-test"), ERROR_NONE);
}

TEST_CASE("service_registration start/stop lifecycle") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "lifecycle-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_manager_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_manager_get_state("lifecycle-test"), SERVICE_STATE_STOPPED);

    CHECK_EQ(service_manager_start("lifecycle-test"), ERROR_NONE);
    CHECK_EQ(on_start_called, 1);
    CHECK_EQ(service_manager_get_state("lifecycle-test"), SERVICE_STATE_STARTED);
    CHECK_NE(service_manager_find_context("lifecycle-test"), nullptr);

    // Starting again while already started should fail
    CHECK_EQ(service_manager_start("lifecycle-test"), ERROR_INVALID_STATE);

    // Removing while running should fail
    CHECK_EQ(service_manager_remove("lifecycle-test"), ERROR_INVALID_STATE);

    CHECK_EQ(service_manager_stop("lifecycle-test"), ERROR_NONE);
    CHECK_EQ(on_stop_called, 1);
    CHECK_EQ(service_manager_get_state("lifecycle-test"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_manager_find_context("lifecycle-test"), nullptr);

    // Stopping again while already stopped should fail
    CHECK_EQ(service_manager_stop("lifecycle-test"), ERROR_NOT_FOUND);

    CHECK_EQ(service_manager_remove("lifecycle-test"), ERROR_NONE);
}

TEST_CASE("service_manager_add with auto_start") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "auto-start-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_manager_add(&manifest, true), ERROR_NONE);
    CHECK_EQ(on_start_called, 1);
    CHECK_EQ(service_manager_get_state("auto-start-test"), SERVICE_STATE_STARTED);

    CHECK_EQ(service_manager_stop("auto-start-test"), ERROR_NONE);
    CHECK_EQ(service_manager_remove("auto-start-test"), ERROR_NONE);
}

TEST_CASE("service_manager_start failure leaves service stopped") {
    reset_counters();
    on_start_result = ERROR_RESOURCE;

    static const ServiceManifest manifest = {
        .id = "failing-start-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_manager_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_manager_start("failing-start-test"), ERROR_RESOURCE);
    CHECK_EQ(service_manager_get_state("failing-start-test"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_manager_find_context("failing-start-test"), nullptr);

    CHECK_EQ(service_manager_remove("failing-start-test"), ERROR_NONE);
}

TEST_CASE("service_registration lookup functions with unknown id") {
    CHECK_EQ(service_manager_get_state("unknown-service-id"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_manager_find_manifest("unknown-service-id"), nullptr);
    CHECK_EQ(service_manager_find_context("unknown-service-id"), nullptr);
    CHECK_EQ(service_manager_start("unknown-service-id"), ERROR_NOT_FOUND);
    CHECK_EQ(service_manager_stop("unknown-service-id"), ERROR_NOT_FOUND);
    CHECK_EQ(service_manager_remove("unknown-service-id"), ERROR_NOT_FOUND);
}
