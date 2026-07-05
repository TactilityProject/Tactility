#include "doctest.h"
#include <tactility/service/service_registration.h>

static int create_called = 0;
static int destroy_called = 0;
static int on_start_called = 0;
static int on_stop_called = 0;
static error_t on_start_result = ERROR_NONE;
static void* last_create_context = nullptr;
static void* last_destroy_context = nullptr;

static Service* test_create_service(void* context) {
    create_called++;
    last_create_context = context;
    static Service service;
    service.data = nullptr;
    service.on_start = [](Service*, ServiceContext*) -> error_t {
        on_start_called++;
        return on_start_result;
    };
    service.on_stop = [](Service*, ServiceContext*) {
        on_stop_called++;
    };
    return &service;
}

static void test_destroy_service(Service*, void* context) {
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

    ServiceInstance instance = { .manifest = nullptr, .service = nullptr, .internal = nullptr };

    CHECK_EQ(service_instance_construct(&instance, &manifest), ERROR_NONE);
    CHECK_NE(instance.internal, nullptr);
    CHECK_EQ(instance.manifest, &manifest);
    CHECK_NE(instance.service, nullptr);
    CHECK_EQ(create_called, 1);
    CHECK_EQ(last_create_context, &context_marker);
    CHECK_EQ(service_instance_get_state(&instance), SERVICE_STATE_STOPPED);

    CHECK_EQ(service_instance_destruct(&instance), ERROR_NONE);
    CHECK_EQ(instance.internal, nullptr);
    CHECK_EQ(destroy_called, 1);
    CHECK_EQ(last_destroy_context, &context_marker);
}

TEST_CASE("service_registration_add rejects duplicate ids") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "duplicate-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_registration_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_registration_add(&manifest, false), ERROR_INVALID_ARGUMENT);

    CHECK_EQ(service_registration_remove("duplicate-test"), ERROR_NONE);
}

TEST_CASE("service_registration start/stop lifecycle") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "lifecycle-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_registration_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_registration_get_state("lifecycle-test"), SERVICE_STATE_STOPPED);

    CHECK_EQ(service_registration_start("lifecycle-test"), ERROR_NONE);
    CHECK_EQ(on_start_called, 1);
    CHECK_EQ(service_registration_get_state("lifecycle-test"), SERVICE_STATE_STARTED);
    CHECK_NE(service_registration_find_context("lifecycle-test"), nullptr);
    CHECK_NE(service_registration_find_service("lifecycle-test"), nullptr);

    // Starting again while already started should fail
    CHECK_EQ(service_registration_start("lifecycle-test"), ERROR_INVALID_STATE);

    // Removing while running should fail
    CHECK_EQ(service_registration_remove("lifecycle-test"), ERROR_INVALID_STATE);

    CHECK_EQ(service_registration_stop("lifecycle-test"), ERROR_NONE);
    CHECK_EQ(on_stop_called, 1);
    CHECK_EQ(service_registration_get_state("lifecycle-test"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_registration_find_context("lifecycle-test"), nullptr);

    // Stopping again while already stopped should fail
    CHECK_EQ(service_registration_stop("lifecycle-test"), ERROR_NOT_FOUND);

    CHECK_EQ(service_registration_remove("lifecycle-test"), ERROR_NONE);
}

TEST_CASE("service_registration_add with auto_start") {
    reset_counters();

    static const ServiceManifest manifest = {
        .id = "auto-start-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_registration_add(&manifest, true), ERROR_NONE);
    CHECK_EQ(on_start_called, 1);
    CHECK_EQ(service_registration_get_state("auto-start-test"), SERVICE_STATE_STARTED);

    CHECK_EQ(service_registration_stop("auto-start-test"), ERROR_NONE);
    CHECK_EQ(service_registration_remove("auto-start-test"), ERROR_NONE);
}

TEST_CASE("service_registration_start failure leaves service stopped") {
    reset_counters();
    on_start_result = ERROR_RESOURCE;

    static const ServiceManifest manifest = {
        .id = "failing-start-test",
        .create_service = test_create_service,
        .destroy_service = test_destroy_service
    };

    CHECK_EQ(service_registration_add(&manifest, false), ERROR_NONE);
    CHECK_EQ(service_registration_start("failing-start-test"), ERROR_RESOURCE);
    CHECK_EQ(service_registration_get_state("failing-start-test"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_registration_find_context("failing-start-test"), nullptr);

    CHECK_EQ(service_registration_remove("failing-start-test"), ERROR_NONE);
}

TEST_CASE("service_registration lookup functions with unknown id") {
    CHECK_EQ(service_registration_get_state("unknown-service-id"), SERVICE_STATE_STOPPED);
    CHECK_EQ(service_registration_find_manifest("unknown-service-id"), nullptr);
    CHECK_EQ(service_registration_find_context("unknown-service-id"), nullptr);
    CHECK_EQ(service_registration_find_service("unknown-service-id"), nullptr);
    CHECK_EQ(service_registration_start("unknown-service-id"), ERROR_NOT_FOUND);
    CHECK_EQ(service_registration_stop("unknown-service-id"), ERROR_NOT_FOUND);
    CHECK_EQ(service_registration_remove("unknown-service-id"), ERROR_NOT_FOUND);
}
