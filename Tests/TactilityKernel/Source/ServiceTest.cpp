#include "doctest.h"
#include <tactility/service/service_manager.h>

// Defined in service_instance.cpp. Internal-only, exposed here to test try_get/put gating.
extern "C" void service_instance_set_state(ServiceInstance* instance, ServiceState state);

static int create_called = 0;
static int destroy_called = 0;
static int on_start_called = 0;
static int on_stop_called = 0;
static error_t on_start_result = ERROR_NONE;
static const ServiceManifest* last_create_manifest = nullptr;
static const ServiceManifest* last_destroy_manifest = nullptr;

static void* test_create_service(const ServiceManifest* manifest) {
    create_called++;
    last_create_manifest = manifest;
    return nullptr;
}

static void test_destroy_service(const ServiceManifest* manifest, void*) {
    destroy_called++;
    last_destroy_manifest = manifest;
}

static error_t test_on_start(ServiceInstance*, void*) {
    on_start_called++;
    return on_start_result;
}

static void test_on_stop(ServiceInstance*, void*) {
    on_stop_called++;
}

static void reset_counters() {
    create_called = 0;
    destroy_called = 0;
    on_start_called = 0;
    on_stop_called = 0;
    on_start_result = ERROR_NONE;
    last_create_manifest = nullptr;
    last_destroy_manifest = nullptr;
}

TEST_CASE("ServiceInstance construction and destruction") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "instance-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service,
        .on_start = test_on_start,
        .on_stop = test_on_stop
    };

    ServiceInstance instance = { .manifest = nullptr, .data = nullptr, .internal = nullptr };

    CHECK_EQ(service_instance_construct(&instance, &manifest), ERROR_NONE);
    CHECK_NE(instance.internal, nullptr);
    CHECK_EQ(instance.manifest, &manifest);
    CHECK_EQ(create_called, 1);
    CHECK_EQ(last_create_manifest, &manifest);
    CHECK_EQ(service_instance_get_state(&instance), SERVICE_STATE_STOPPED);

    CHECK_EQ(service_instance_destruct(&instance), ERROR_NONE);
    CHECK_EQ(instance.internal, nullptr);
    CHECK_EQ(destroy_called, 1);
    CHECK_EQ(last_destroy_manifest, &manifest);
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
        .destroy_service = test_destroy_service,
        .on_start = test_on_start,
        .on_stop = test_on_stop
    };

    CHECK_EQ(service_manager_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_manager_get_state("lifecycle-test"), SERVICE_STATE_STOPPED);

    CHECK_EQ(service_manager_start("lifecycle-test"), ERROR_NONE);
    CHECK_EQ(on_start_called, 1);
    CHECK_EQ(service_manager_get_state("lifecycle-test"), SERVICE_STATE_STARTED);
    CHECK_NE(service_manager_find_instance("lifecycle-test"), nullptr);

    // Starting again while already started should fail
    CHECK_EQ(service_manager_start("lifecycle-test"), ERROR_INVALID_STATE);

    // Removing while running should fail
    CHECK_EQ(service_manager_remove("lifecycle-test"), ERROR_INVALID_STATE);

    CHECK_EQ(service_manager_stop("lifecycle-test"), ERROR_NONE);
    CHECK_EQ(on_stop_called, 1);
    CHECK_EQ(service_manager_get_state("lifecycle-test"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_manager_find_instance("lifecycle-test"), nullptr);

    // Stopping again while already stopped should fail
    CHECK_EQ(service_manager_stop("lifecycle-test"), ERROR_NOT_FOUND);

    CHECK_EQ(service_manager_remove("lifecycle-test"), ERROR_NONE);
}

TEST_CASE("service_manager_add with auto_start") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "auto-start-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service,
        .on_start = test_on_start,
        .on_stop = test_on_stop
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
        .destroy_service = test_destroy_service,
        .on_start = test_on_start,
        .on_stop = test_on_stop
    };

    CHECK_EQ(service_manager_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_manager_start("failing-start-test"), ERROR_RESOURCE);
    CHECK_EQ(service_manager_get_state("failing-start-test"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_manager_find_instance("failing-start-test"), nullptr);

    CHECK_EQ(service_manager_remove("failing-start-test"), ERROR_NONE);
}

TEST_CASE("service_registration lookup functions with unknown id") {
    CHECK_EQ(service_manager_get_state("unknown-service-id"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_manager_find_manifest("unknown-service-id"), nullptr);
    CHECK_EQ(service_manager_find_instance("unknown-service-id"), nullptr);
    CHECK_EQ(service_manager_start("unknown-service-id"), ERROR_NOT_FOUND);
    CHECK_EQ(service_manager_stop("unknown-service-id"), ERROR_NOT_FOUND);
    CHECK_EQ(service_manager_remove("unknown-service-id"), ERROR_NOT_FOUND);
}
