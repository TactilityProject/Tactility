#include "doctest.h"

#include <app_hub/app_hub.h>
#include <app_hub/app_hub_service.h>

#include <app/manager.h>

#include <service/manager.h>
#include <service/paths.h>

#include <tactility/delay.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/paths.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const char* TEST_ROOT = "/tmp/tactility_app_hub_test";

const char* CATALOG = R"({
  "apps": [
    {
      "appId": "beta", "appVersionName": "1.0", "appVersionCode": 1, "appName": "beta tool",
      "appDescription": "The beta tool", "targetSdk": "0.7.0", "file": "beta.app", "targetPlatforms": []
    },
    {
      "appId": "alpha", "appVersionName": "2.0", "appVersionCode": 2, "appName": "Alpha App",
      "appDescription": "The alpha app", "targetSdk": "0.7.0", "file": "alpha.app", "targetPlatforms": []
    },
    {
      "appId": "other", "appVersionName": "1.0", "appVersionCode": 1, "appName": "Other device",
      "appDescription": "", "targetSdk": "0.7.0", "file": "other.app", "targetPlatforms": [],
      "requiresDeviceId": ["no-such-device"]
    }
  ]
})";

error_t fakeMount(void*) { return ERROR_NONE; }
error_t fakeUnmount(void*) { return ERROR_NONE; }
bool fakeIsMounted(void*) { return false; }

error_t fakeGetPath(void* data, char* out_path, size_t out_path_size) {
    const char* path = static_cast<const char*>(data);
    if (strlen(path) + 1 > out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(out_path, path);
    return ERROR_NONE;
}

const FileSystemApi fakeDataApi = {
    .mount = fakeMount,
    .unmount = fakeUnmount,
    .is_mounted = fakeIsMounted,
    .get_path = fakeGetPath,
};

/** The service caches the catalog once it's loaded, so all tests share one data directory and catalog */
void ensureFixture() {
    static bool created = false;
    if (created) {
        return;
    }
    created = true;

    std::filesystem::remove_all(TEST_ROOT);
    REQUIRE_NE(file_system_add("data", &fakeDataApi, const_cast<char*>(TEST_ROOT)), nullptr);

    char path[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(service_paths_get_user_data_path(APP_HUB_SERVICE_ID, "apps.json", path, sizeof(path)), ERROR_NONE);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    FILE* file = fopen(path, "w");
    REQUIRE_NE(file, nullptr);
    fputs(CATALOG, file);
    fclose(file);

    REQUIRE_EQ(service_manager_add(&app_hub_service_manifest, true), ERROR_NONE);
}

std::vector<AppHubApp> search(const char* query) {
    std::vector<AppHubApp> apps;
    REQUIRE_EQ(app_hub_search(query, [](const AppHubApp* app, void* context) {
        static_cast<std::vector<AppHubApp>*>(context)->push_back(*app);
        return true;
    }, &apps), ERROR_NONE);
    return apps;
}

/** Waits for the next event of the subscription */
AppHubEvent waitForEvent(AppHubEventSubscription& sub) {
    AppHubEvent event {};
    REQUIRE_EQ(app_hub_event_await(&sub, pdMS_TO_TICKS(5000)), ERROR_NONE);
    REQUIRE_EQ(app_hub_event_poll(&sub, &event), ERROR_NONE);
    return event;
}

} // namespace

TEST_CASE("app_hub_search lists compatible catalog apps sorted by name") {
    ensureFixture();
    const auto apps = search(nullptr);
    REQUIRE_EQ(apps.size(), 2);
    CHECK_EQ(std::string(apps[0].id), "alpha");
    CHECK_EQ(std::string(apps[0].name), "Alpha App");
    CHECK_EQ(std::string(apps[0].version_name), "2.0");
    CHECK_EQ(apps[0].version_code, 2);
    CHECK(apps[0].in_catalog);
    CHECK_FALSE(apps[0].installed);
    CHECK_EQ(std::string(apps[1].id), "beta");
}

TEST_CASE("app_hub_search filters by name and id ignoring case") {
    ensureFixture();
    auto apps = search("TOOL");
    REQUIRE_EQ(apps.size(), 1);
    CHECK_EQ(std::string(apps[0].id), "beta");

    apps = search("alp");
    REQUIRE_EQ(apps.size(), 1);
    CHECK_EQ(std::string(apps[0].id), "alpha");

    CHECK(search("nothing matches").empty());
    CHECK_EQ(search("").size(), 2);
}

TEST_CASE("app_hub_get_description returns the catalog description") {
    ensureFixture();
    char description[64];
    CHECK_EQ(app_hub_get_description("alpha", description, sizeof(description)), ERROR_NONE);
    CHECK_EQ(std::string(description), "The alpha app");
    CHECK_EQ(app_hub_get_description("alpha", description, 4), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(app_hub_get_description("missing", description, sizeof(description)), ERROR_NOT_FOUND);
}

TEST_CASE("app_hub merges installed packages with the catalog") {
    ensureFixture();
    PackageManifest alpha {};
    strcpy(alpha.id, "alpha");
    strcpy(alpha.version_name, "1.0");
    alpha.version_code = 1;
    PackageManifest sideloaded {};
    strcpy(sideloaded.id, "sideloaded");
    strcpy(sideloaded.version_name, "3.0");
    sideloaded.version_code = 3;
    REQUIRE_EQ(app_manager_add_package(&alpha, nullptr, 0), ERROR_NONE);
    REQUIRE_EQ(app_manager_add_package(&sideloaded, nullptr, 0), ERROR_NONE);

    AppHubApp app {};
    REQUIRE_EQ(app_hub_get_app("alpha", &app), ERROR_NONE);
    CHECK(app.installed);
    CHECK(app.in_catalog);
    CHECK(app.update_available);
    CHECK_EQ(std::string(app.installed_version_name), "1.0");

    REQUIRE_EQ(app_hub_get_app("sideloaded", &app), ERROR_NONE);
    CHECK(app.installed);
    CHECK_FALSE(app.in_catalog);
    CHECK_FALSE(app.update_available);
    CHECK_EQ(std::string(app.name), "sideloaded");

    CHECK_EQ(search(nullptr).size(), 3);
    CHECK_EQ(app_hub_get_app("other", &app), ERROR_NOT_FOUND);

    CHECK_EQ(app_manager_remove_package("alpha"), ERROR_NONE);
    CHECK_EQ(app_manager_remove_package("sideloaded"), ERROR_NONE);
}

TEST_CASE("app_hub operations report their outcome and the worker stops when idle") {
    ensureFixture();
    TaskEventGroup group {};
    task_event_group_construct(&group);
    AppHubEventSubscription sub {};
    REQUIRE_EQ(app_hub_event_subscribe(&sub, &group), ERROR_NONE);

    uint32_t removeId = 0;
    REQUIRE_EQ(app_hub_remove("missing", &removeId), ERROR_NONE);
    CHECK(app_hub_is_worker_running());
    AppHubEvent event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_REMOVING);
    CHECK_EQ(event.operation_id, removeId);
    CHECK_EQ(std::string(event.app_id), "missing");
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_REMOVED);
    CHECK_EQ(std::string(event.app_id), "missing");
    CHECK_EQ(event.operation_id, removeId);
    CHECK_EQ(event.result, ERROR_NOT_FOUND);

    uint32_t installId = 0;
    REQUIRE_EQ(app_hub_install("unknown", &installId), ERROR_NONE);
    CHECK_NE(installId, removeId);
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_INSTALLING);
    CHECK_EQ(std::string(event.app_id), "unknown");
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_INSTALLED);
    CHECK_EQ(std::string(event.app_id), "unknown");
    CHECK_EQ(event.operation_id, installId);
    CHECK_EQ(event.result, ERROR_NOT_FOUND);

    delay_millis(6000);
    CHECK_FALSE(app_hub_is_worker_running());

    // A new operation starts the worker again
    REQUIRE_EQ(app_hub_remove("missing", &removeId), ERROR_NONE);
    CHECK(app_hub_is_worker_running());
    CHECK_EQ(waitForEvent(sub).type, APP_HUB_EVENT_REMOVING);
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_REMOVED);
    CHECK_EQ(event.operation_id, removeId);

    CHECK_EQ(app_hub_event_await(&sub, pdMS_TO_TICKS(10)), ERROR_TIMEOUT);
    CHECK_EQ(app_hub_event_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("app_hub queues operations of different apps and runs them in order") {
    ensureFixture();
    TaskEventGroup group {};
    task_event_group_construct(&group);
    AppHubEventSubscription sub {};
    REQUIRE_EQ(app_hub_event_subscribe(&sub, &group), ERROR_NONE);

    uint32_t firstId = 0;
    uint32_t secondId = 0;
    REQUIRE_EQ(app_hub_remove("first", &firstId), ERROR_NONE);
    REQUIRE_EQ(app_hub_install("second", &secondId), ERROR_NONE);
    CHECK_NE(firstId, secondId);

    AppHubEvent event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_REMOVING);
    CHECK_EQ(event.operation_id, firstId);
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_REMOVED);
    CHECK_EQ(event.operation_id, firstId);
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_INSTALLING);
    CHECK_EQ(event.operation_id, secondId);
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_INSTALLED);
    CHECK_EQ(event.operation_id, secondId);

    CHECK_EQ(app_hub_event_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("app_hub drops newer events while a subscriber's queue is full") {
    ensureFixture();
    TaskEventGroup group {};
    task_event_group_construct(&group);
    AppHubEventSubscription polled {};
    AppHubEventSubscription unpolled {};
    REQUIRE_EQ(app_hub_event_subscribe(&polled, &group), ERROR_NONE);
    REQUIRE_EQ(app_hub_event_subscribe(&unpolled, &group), ERROR_NONE);

    // Each operation emits 2 events, so the last operation's events don't fit
    uint32_t ids[APP_HUB_EVENT_QUEUE_CAPACITY / 2 + 1] = {};
    for (auto& id : ids) {
        REQUIRE_EQ(app_hub_remove("missing", &id), ERROR_NONE);
        CHECK_EQ(waitForEvent(polled).type, APP_HUB_EVENT_REMOVING);
        CHECK_EQ(waitForEvent(polled).type, APP_HUB_EVENT_REMOVED);
    }

    std::vector<uint32_t> received;
    AppHubEvent event {};
    while (app_hub_event_poll(&unpolled, &event) == ERROR_NONE) {
        received.push_back(event.operation_id);
    }
    REQUIRE_EQ(received.size(), APP_HUB_EVENT_QUEUE_CAPACITY);
    CHECK_EQ(received.front(), ids[0]);
    CHECK_EQ(received.back(), ids[APP_HUB_EVENT_QUEUE_CAPACITY / 2 - 1]);

    CHECK_EQ(app_hub_event_unsubscribe(&unpolled), ERROR_NONE);
    CHECK_EQ(app_hub_event_unsubscribe(&polled), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("app_hub_update_all finishes without installs when nothing has an update") {
    ensureFixture();
    TaskEventGroup group {};
    task_event_group_construct(&group);
    AppHubEventSubscription sub {};
    REQUIRE_EQ(app_hub_event_subscribe(&sub, &group), ERROR_NONE);

    uint32_t id = 0;
    REQUIRE_EQ(app_hub_update_all(&id), ERROR_NONE);
    AppHubEvent event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_UPDATING);
    CHECK_EQ(event.operation_id, id);
    CHECK_EQ(std::string(event.app_id), "");
    event = waitForEvent(sub);
    CHECK_EQ(event.type, APP_HUB_EVENT_UPDATED);
    CHECK_EQ(event.operation_id, id);
    CHECK_EQ(event.result, ERROR_NONE);

    CHECK_EQ(app_hub_event_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("app_hub rejects invalid ids") {
    ensureFixture();
    CHECK_EQ(app_hub_install("", nullptr), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_hub_remove("an-id-that-is-far-too-long-for-a-package", nullptr), ERROR_INVALID_ARGUMENT);
}
