#include "doctest.h"

#include <notification/module.h>
#include <notification/notification.h>

#include <service/paths.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>
#include <tactility/paths.h>
#include <tactility/filesystem/fs.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

/** Waits until the queued file operations ran, which are done in order */
void drainFileOps() {
    static std::atomic<bool> done;
    done = false;
    if (file_ops_queue([](void*) { done = true; }, nullptr, nullptr) != ERROR_NONE) {
        return;
    }
    for (int i = 0; i < 2000 && !done; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

/** Removes the stored sources, so one run doesn't fill the limit for the next and tests start from nothing */
void removeStoredSources() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_path("tactility.notification", "sources.properties", path, sizeof(path)) == ERROR_NONE) {
        remove(path);
    }
}

/** A started module with an empty history and no stored sources, and a stopped one when it goes out of scope */
struct ModuleFixture {
    ModuleFixture() {
        module_stop(&notification_module);
        drainFileOps();
        removeStoredSources();
        REQUIRE_EQ(module_ensure_started(&notification_module), ERROR_NONE);
    }
    ~ModuleFixture() {
        module_stop(&notification_module);
        // A save that is still queued would write the sources again after they were removed
        drainFileOps();
        removeStoredSources();
    }
};

/** The sources are stored, so a test uses ids that no earlier run has stored. An id has at most 32 characters. */
std::string uniqueId(const char* name) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string("t.") + name + "." + std::to_string(ticks % 100000000);
}

uint32_t send(const char* message, const char* title = nullptr) {
    NotificationContext context = notification_context_for_app("tactility.test", message);
    if (title != nullptr) {
        notification_context_set_title(&context, title);
    }
    uint32_t id = 0;
    REQUIRE_EQ(notification_send(&context, &id), ERROR_NONE);
    return id;
}

} // namespace

TEST_CASE("a context has defaults and truncates text at a UTF-8 boundary") {
    NotificationContext app = notification_context_for_app("tactility.test", "hello");
    CHECK_EQ(app.source_type, NOTIFICATION_SOURCE_APP);
    CHECK_EQ(std::string(app.source_id), "tactility.test");
    CHECK_EQ(std::string(app.title), "");
    CHECK_EQ(app.severity, NOTIFICATION_SEVERITY_INFO);
    CHECK_EQ(app.data, nullptr);

    NotificationContext service = notification_context_for_service("tactility.mesh", "hello");
    CHECK_EQ(service.source_type, NOTIFICATION_SOURCE_SERVICE);

    // Each "é" is 2 bytes, so an odd limit must not split one
    std::string title;
    for (int i = 0; i < 40; i++) {
        title += "\xC3\xA9";
    }
    notification_context_set_title(&app, title.c_str());
    const size_t length = strlen(app.title);
    CHECK_LE(length, NOTIFICATION_TITLE_MAX_LENGTH);
    CHECK_EQ(length % 2, 0);

    const std::string longMessage(500, 'x');
    NotificationContext longContext = notification_context_for_app("tactility.test", longMessage.c_str());
    CHECK_EQ(strlen(longContext.message), NOTIFICATION_MESSAGE_MAX_LENGTH);
}

TEST_CASE("send needs a message and a source id, and a started module") {
    NotificationContext noMessage = notification_context_for_app("tactility.test", "");
    NotificationContext noSource = notification_context_for_app("", "hello");
    NotificationContext valid = notification_context_for_app("tactility.test", "hello");

    module_stop(&notification_module);
    CHECK_EQ(notification_send(&valid, nullptr), ERROR_INVALID_STATE);

    ModuleFixture fixture;
    CHECK_EQ(notification_send(&noMessage, nullptr), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(notification_send(&noSource, nullptr), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(notification_send(&valid, nullptr), ERROR_NONE);
}

TEST_CASE("a sent notification is stamped and can be read back") {
    ModuleFixture fixture;
    NotificationContext context = notification_context_for_service("tactility.mesh", "message");
    notification_context_set_title(&context, "title");
    notification_context_set_severity(&context, NOTIFICATION_SEVERITY_WARNING);
    uint32_t id = 0;
    REQUIRE_EQ(notification_send(&context, &id), ERROR_NONE);
    CHECK_NE(id, 0);

    Notification notification {};
    REQUIRE_EQ(notification_get(id, &notification), ERROR_NONE);
    CHECK_EQ(notification.id, id);
    CHECK_EQ(notification.source_type, NOTIFICATION_SOURCE_SERVICE);
    CHECK_EQ(std::string(notification.source_id), "tactility.mesh");
    CHECK_EQ(std::string(notification.title), "title");
    CHECK_EQ(std::string(notification.message), "message");
    CHECK_EQ(notification.severity, NOTIFICATION_SEVERITY_WARNING);
    CHECK_EQ(notification.data, nullptr);
    notification_release(&notification);

    const uint32_t second = send("again");
    CHECK_GT(second, id);
}

TEST_CASE("notification data is a Bundle that is created on demand") {
    ModuleFixture fixture;
    NotificationContext context = notification_context_for_app("tactility.test", "with data");
    CHECK_EQ(context.data, nullptr);
    Bundle* data = notification_context_get_data(&context);
    REQUIRE_NE(data, nullptr);
    CHECK_EQ(notification_context_get_data(&context), data);

    const std::string longValue(1000, 'v');
    bundle_put_string(data, "peer", "abcd");
    bundle_put_string(data, "long", longValue.c_str());
    bundle_put_int32(data, "count", 7);
    bundle_put_bool(data, "flag", true);
    uint32_t id = 0;
    REQUIRE_EQ(notification_send(&context, &id), ERROR_NONE);
    // The Bundle moved to the notification
    CHECK_EQ(context.data, nullptr);

    Notification notification {};
    REQUIRE_EQ(notification_get(id, &notification), ERROR_NONE);
    REQUIRE_NE(notification.data, nullptr);
    CHECK_NE(notification.data, data);
    char text[16];
    CHECK_EQ(bundle_opt_string(notification.data, "peer", text, sizeof(text)), ERROR_NONE);
    CHECK_EQ(std::string(text), "abcd");
    std::string longRead(1001, '\0');
    CHECK_EQ(bundle_get_string(notification.data, "long", longRead.data(), longRead.size()), ERROR_NONE);
    CHECK_EQ(std::string(longRead.c_str()), longValue);
    CHECK_EQ(bundle_get_int32(notification.data, "count"), 7);
    CHECK(bundle_get_bool(notification.data, "flag"));

    // The reader's copy outlives the stored notification
    REQUIRE_EQ(notification_dismiss(id), ERROR_NONE);
    CHECK(bundle_has_int32(notification.data, "count"));
    notification_release(&notification);
    CHECK_EQ(notification.data, nullptr);

    NotificationContext unsent = notification_context_for_app("tactility.test", "never sent");
    notification_context_get_data(&unsent);
    notification_context_destroy(&unsent);
    CHECK_EQ(unsent.data, nullptr);
}

TEST_CASE("history is newest first and the oldest notification is removed when it is full") {
    ModuleFixture fixture;
    uint32_t first = 0;
    for (int i = 0; i < NOTIFICATION_HISTORY_CAPACITY + 2; i++) {
        const uint32_t id = send(("message " + std::to_string(i)).c_str());
        if (i == 0) {
            first = id;
        }
    }
    Notification all[NOTIFICATION_HISTORY_CAPACITY + 4];
    size_t count = 0;
    REQUIRE_EQ(notification_get_all(all, NOTIFICATION_HISTORY_CAPACITY + 4, &count), ERROR_NONE);
    REQUIRE_EQ(count, NOTIFICATION_HISTORY_CAPACITY);
    CHECK_EQ(std::string(all[0].message), "message 17");
    CHECK_EQ(std::string(all[NOTIFICATION_HISTORY_CAPACITY - 1].message), "message 2");
    for (size_t i = 0; i < count; i++) {
        notification_release(&all[i]);
    }

    Notification missing {};
    CHECK_EQ(notification_get(first, &missing), ERROR_NOT_FOUND);

    // The limit of the buffer applies
    REQUIRE_EQ(notification_get_all(all, 3, &count), ERROR_NONE);
    CHECK_EQ(count, 3);
    for (size_t i = 0; i < count; i++) {
        notification_release(&all[i]);
    }
}

TEST_CASE("dismissing removes notifications") {
    ModuleFixture fixture;
    const uint32_t a = send("a");
    const uint32_t b = send("b");
    CHECK_EQ(notification_dismiss(a), ERROR_NONE);
    CHECK_EQ(notification_dismiss(a), ERROR_NOT_FOUND);

    Notification notification {};
    CHECK_EQ(notification_get(a, &notification), ERROR_NOT_FOUND);
    REQUIRE_EQ(notification_get(b, &notification), ERROR_NONE);
    notification_release(&notification);

    notification_dismiss_all();
    CHECK_EQ(notification_get(b, &notification), ERROR_NOT_FOUND);
    size_t count = 99;
    Notification all[2];
    REQUIRE_EQ(notification_get_all(all, 2, &count), ERROR_NONE);
    CHECK_EQ(count, 0);
}

TEST_CASE("subscribers get events in order") {
    ModuleFixture fixture;
    TaskEventGroup group {};
    task_event_group_construct(&group);
    NotificationSubscription first {};
    NotificationSubscription second {};
    REQUIRE_EQ(notification_subscribe(&first, &group), ERROR_NONE);
    REQUIRE_EQ(notification_subscribe(&second, &group), ERROR_NONE);
    CHECK_EQ(notification_subscribe(&first, &group), ERROR_INVALID_STATE);

    const uint32_t a = send("a");
    const uint32_t b = send("b");
    REQUIRE_EQ(notification_dismiss(a), ERROR_NONE);
    notification_dismiss_all();

    NotificationEvent event {};
    for (auto* sub : { &first, &second }) {
        REQUIRE_EQ(notification_poll(sub, &event), ERROR_NONE);
        CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
        CHECK_EQ(event.id, a);
        REQUIRE_EQ(notification_poll(sub, &event), ERROR_NONE);
        CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
        CHECK_EQ(event.id, b);
        REQUIRE_EQ(notification_poll(sub, &event), ERROR_NONE);
        CHECK_EQ(event.type, NOTIFICATION_EVENT_DISMISSED);
        CHECK_EQ(event.id, a);
        REQUIRE_EQ(notification_poll(sub, &event), ERROR_NONE);
        CHECK_EQ(event.type, NOTIFICATION_EVENT_CLEARED);
        CHECK_EQ(event.id, 0);
        CHECK_EQ(notification_poll(sub, &event), ERROR_TIMEOUT);
    }

    CHECK_EQ(notification_unsubscribe(&second), ERROR_NONE);
    CHECK_EQ(notification_unsubscribe(&second), ERROR_NOT_FOUND);
    send("c");
    CHECK_EQ(notification_poll(&second, &event), ERROR_TIMEOUT);
    CHECK_EQ(notification_poll(&first, &event), ERROR_NONE);

    CHECK_EQ(notification_unsubscribe(&first), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("removing the oldest notification of a full history is an event, and a full queue drops the newest event") {
    ModuleFixture fixture;
    TaskEventGroup group {};
    task_event_group_construct(&group);
    NotificationSubscription sub {};
    REQUIRE_EQ(notification_subscribe(&sub, &group), ERROR_NONE);

    uint32_t first = 0;
    for (int i = 0; i < NOTIFICATION_HISTORY_CAPACITY; i++) {
        const uint32_t id = send("fill");
        if (i == 0) {
            first = id;
        }
    }
    NotificationEvent event {};
    // The queue holds the oldest events and drops the rest
    for (int i = 0; i < NOTIFICATION_EVENT_QUEUE_CAPACITY; i++) {
        REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
        CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
    }
    CHECK_EQ(notification_poll(&sub, &event), ERROR_TIMEOUT);

    const uint32_t last = send("one more");
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_DISMISSED);
    CHECK_EQ(event.id, first);
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
    CHECK_EQ(event.id, last);

    CHECK_EQ(notification_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

namespace {

struct ActionCalls {
    int count = 0;
    uint32_t id = 0;
    void* data = nullptr;
};

void recordAction(uint32_t id, void* data) {
    auto* calls = static_cast<ActionCalls*>(data);
    calls->count++;
    calls->id = id;
    calls->data = data;
    // The module's lock isn't held, so the action can use the module
    Notification notification {};
    CHECK_EQ(notification_get(id, &notification), ERROR_NONE);
    notification_release(&notification);
    CHECK_EQ(notification_dismiss(id), ERROR_NONE);
}

} // namespace

TEST_CASE("an action is called with the notification id and user data") {
    ModuleFixture fixture;
    ActionCalls calls;
    NotificationContext context = notification_context_for_app("tactility.test", "with action");
    notification_context_set_action(&context, recordAction, &calls);
    uint32_t id = 0;
    REQUIRE_EQ(notification_send(&context, &id), ERROR_NONE);

    Notification notification {};
    REQUIRE_EQ(notification_get(id, &notification), ERROR_NONE);
    CHECK_EQ(notification.action, recordAction);
    CHECK_EQ(notification.action_data, &calls);
    notification_release(&notification);

    CHECK_EQ(notification_invoke_action(id), ERROR_NONE);
    CHECK_EQ(calls.count, 1);
    CHECK_EQ(calls.id, id);
    // The action dismissed it
    CHECK_EQ(notification_invoke_action(id), ERROR_NOT_FOUND);
    CHECK_EQ(calls.count, 1);
}

TEST_CASE("a notification without an action can't be invoked") {
    ModuleFixture fixture;
    const uint32_t id = send("plain");
    CHECK_EQ(notification_invoke_action(id), ERROR_NOT_SUPPORTED);
    CHECK_EQ(notification_invoke_action(id + 100), ERROR_NOT_FOUND);
    Notification notification {};
    REQUIRE_EQ(notification_get(id, &notification), ERROR_NONE);
    CHECK_EQ(notification.action, nullptr);
    notification_release(&notification);
}

TEST_CASE("an update replaces the content, keeps the id and makes it the newest") {
    ModuleFixture fixture;
    TaskEventGroup group {};
    task_event_group_construct(&group);
    NotificationSubscription sub {};
    REQUIRE_EQ(notification_subscribe(&sub, &group), ERROR_NONE);

    NotificationContext first = notification_context_for_service("tactility.mesh", "1 new message");
    notification_context_set_title(&first, "New messages");
    Bundle* oldData = notification_context_get_data(&first);
    bundle_put_int32(oldData, "count", 1);
    uint32_t id = 0;
    REQUIRE_EQ(notification_send(&first, &id), ERROR_NONE);
    const uint32_t other = send("other");

    NotificationContext update = notification_context_for_service("tactility.mesh", "2 new messages");
    notification_context_set_title(&update, "New messages");
    bundle_put_int32(notification_context_get_data(&update), "count", 2);
    REQUIRE_EQ(notification_update(id, &update, true), ERROR_NONE);
    CHECK_EQ(update.data, nullptr);

    Notification all[4];
    size_t count = 0;
    REQUIRE_EQ(notification_get_all(all, 4, &count), ERROR_NONE);
    REQUIRE_EQ(count, 2);
    CHECK_EQ(all[0].id, id);
    CHECK_EQ(all[1].id, other);
    CHECK_EQ(std::string(all[0].message), "2 new messages");
    REQUIRE_NE(all[0].data, nullptr);
    CHECK_EQ(bundle_get_int32(all[0].data, "count"), 2);
    for (size_t i = 0; i < count; i++) {
        notification_release(&all[i]);
    }

    NotificationEvent event {};
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_UPDATED);
    CHECK_EQ(event.id, id);

    // A dismissed notification can't be updated, and the caller keeps the context
    REQUIRE_EQ(notification_dismiss(id), ERROR_NONE);
    NotificationContext late = notification_context_for_service("tactility.mesh", "late");
    notification_context_get_data(&late);
    CHECK_EQ(notification_update(id, &late, true), ERROR_NOT_FOUND);
    CHECK_NE(late.data, nullptr);
    notification_context_destroy(&late);

    NotificationContext empty = notification_context_for_service("tactility.mesh", "");
    CHECK_EQ(notification_update(other, &empty, true), ERROR_INVALID_ARGUMENT);

    CHECK_EQ(notification_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("the count follows sending and dismissing") {
    ModuleFixture fixture;
    CHECK_EQ(notification_get_count(), 0);
    const uint32_t a = send("a");
    send("b");
    CHECK_EQ(notification_get_count(), 2);
    notification_dismiss(a);
    CHECK_EQ(notification_get_count(), 1);
    notification_dismiss_all();
    CHECK_EQ(notification_get_count(), 0);
}

TEST_CASE("the first notification of a sender adds it to the sources") {
    ModuleFixture fixture;
    NotificationSource sources[NOTIFICATION_SOURCE_LIMIT];
    size_t count = 0;
    REQUIRE_EQ(notification_get_sources(sources, NOTIFICATION_SOURCE_LIMIT, &count), ERROR_NONE);
    const size_t before = count;

    const std::string appId = uniqueId("src.app");
    NotificationContext app = notification_context_for_app(appId.c_str(), "hello");
    REQUIRE_EQ(notification_send(&app, nullptr), ERROR_NONE);
    NotificationContext service = notification_context_for_service(uniqueId("src.svc").c_str(), "hello");
    REQUIRE_EQ(notification_send(&service, nullptr), ERROR_NONE);
    // A second one doesn't add it again
    NotificationContext again = notification_context_for_app(appId.c_str(), "again");
    REQUIRE_EQ(notification_send(&again, nullptr), ERROR_NONE);

    REQUIRE_EQ(notification_get_sources(sources, NOTIFICATION_SOURCE_LIMIT, &count), ERROR_NONE);
    CHECK_EQ(count, before + 2);
    CHECK_EQ(sources[count - 2].type, NOTIFICATION_SOURCE_APP);
    CHECK_EQ(std::string(sources[count - 2].id), appId);
    CHECK(sources[count - 2].enabled);
    CHECK_EQ(sources[count - 1].type, NOTIFICATION_SOURCE_SERVICE);
}

TEST_CASE("disabling a sender removes its notifications and refuses new ones") {
    ModuleFixture fixture;
    TaskEventGroup group {};
    task_event_group_construct(&group);
    NotificationSubscription sub {};
    REQUIRE_EQ(notification_subscribe(&sub, &group), ERROR_NONE);

    const std::string senderId = uniqueId("off");
    uint32_t first = 0;
    NotificationContext one = notification_context_for_app(senderId.c_str(), "one");
    REQUIRE_EQ(notification_send(&one, &first), ERROR_NONE);
    const uint32_t other = send("other sender");
    NotificationContext two = notification_context_for_app(senderId.c_str(), "two");
    uint32_t second = 0;
    REQUIRE_EQ(notification_send(&two, &second), ERROR_NONE);
    NotificationEvent event {};
    for (int i = 0; i < 3; i++) {
        REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    }

    REQUIRE_EQ(notification_set_source_enabled(NOTIFICATION_SOURCE_APP, senderId.c_str(), false), ERROR_NONE);
    Notification notification {};
    CHECK_EQ(notification_get(first, &notification), ERROR_NOT_FOUND);
    CHECK_EQ(notification_get(second, &notification), ERROR_NOT_FOUND);
    REQUIRE_EQ(notification_get(other, &notification), ERROR_NONE);
    notification_release(&notification);
    // Newest first
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_DISMISSED);
    CHECK_EQ(event.id, second);
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.id, first);

    // The caller keeps the context of a refused notification
    NotificationContext refused = notification_context_for_app(senderId.c_str(), "refused");
    notification_context_get_data(&refused);
    CHECK_EQ(notification_send(&refused, nullptr), ERROR_NOT_ALLOWED);
    CHECK_NE(refused.data, nullptr);
    notification_context_destroy(&refused);

    REQUIRE_EQ(notification_set_source_enabled(NOTIFICATION_SOURCE_APP, senderId.c_str(), true), ERROR_NONE);
    NotificationContext accepted = notification_context_for_app(senderId.c_str(), "accepted");
    CHECK_EQ(notification_send(&accepted, nullptr), ERROR_NONE);

    CHECK_EQ(notification_set_source_enabled(NOTIFICATION_SOURCE_APP, "tactility.never.sent", false), ERROR_NOT_FOUND);
    CHECK_EQ(notification_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("a notification is sticky only when the sender made it sticky") {
    ModuleFixture fixture;
    const uint32_t plain = send("plain");
    NotificationContext context = notification_context_for_app("tactility.test", "sticky");
    notification_context_set_sticky(&context, true);
    uint32_t sticky = 0;
    REQUIRE_EQ(notification_send(&context, &sticky), ERROR_NONE);

    Notification notification {};
    REQUIRE_EQ(notification_get(plain, &notification), ERROR_NONE);
    CHECK_FALSE(notification.sticky);
    notification_release(&notification);
    REQUIRE_EQ(notification_get(sticky, &notification), ERROR_NONE);
    CHECK(notification.sticky);
    notification_release(&notification);

    // An update takes the flag of its context
    NotificationContext update = notification_context_for_app("tactility.test", "not sticky anymore");
    REQUIRE_EQ(notification_update(sticky, &update, true), ERROR_NONE);
    REQUIRE_EQ(notification_get(sticky, &notification), ERROR_NONE);
    CHECK_FALSE(notification.sticky);
    notification_release(&notification);
}

TEST_CASE("a context started from a notification changes only what the sender sets") {
    ModuleFixture fixture;
    NotificationContext first = notification_context_for_app("tactility.test", "1 file");
    notification_context_set_title(&first, "Download");
    notification_context_set_severity(&first, NOTIFICATION_SEVERITY_WARNING);
    bundle_put_int32(notification_context_get_data(&first), "count", 1);
    uint32_t id = 0;
    REQUIRE_EQ(notification_send(&first, &id), ERROR_NONE);

    NotificationContext update {};
    REQUIRE_EQ(notification_context_for_notification(id, &update), ERROR_NONE);
    CHECK_EQ(std::string(update.title), "Download");
    CHECK_EQ(update.severity, NOTIFICATION_SEVERITY_WARNING);
    REQUIRE_NE(update.data, nullptr);
    CHECK_EQ(bundle_get_int32(update.data, "count"), 1);
    snprintf(update.message, sizeof(update.message), "2 files");
    bundle_put_int32(update.data, "count", 2);
    REQUIRE_EQ(notification_update(id, &update, true), ERROR_NONE);

    Notification notification {};
    REQUIRE_EQ(notification_get(id, &notification), ERROR_NONE);
    CHECK_EQ(std::string(notification.title), "Download");
    CHECK_EQ(std::string(notification.message), "2 files");
    CHECK_EQ(bundle_get_int32(notification.data, "count"), 2);
    notification_release(&notification);

    CHECK_EQ(notification_dismiss(id), ERROR_NONE);
    CHECK_EQ(notification_context_for_notification(id, &update), ERROR_NOT_FOUND);
}

TEST_CASE("an update without bump keeps its place and timestamps") {
    ModuleFixture fixture;
    const uint32_t older = send("older");
    const uint32_t newer = send("newer");

    Notification before {};
    REQUIRE_EQ(notification_get(older, &before), ERROR_NONE);
    notification_release(&before);

    NotificationContext keep = notification_context_for_app("tactility.test", "older changed");
    REQUIRE_EQ(notification_update(older, &keep, false), ERROR_NONE);

    Notification all[4];
    size_t count = 0;
    REQUIRE_EQ(notification_get_all(all, 4, &count), ERROR_NONE);
    REQUIRE_EQ(count, 2);
    CHECK_EQ(all[0].id, newer);
    CHECK_EQ(all[1].id, older);
    CHECK_EQ(std::string(all[1].message), "older changed");
    CHECK_EQ(all[1].uptime_ms, before.uptime_ms);
    CHECK_EQ(all[1].wall_time, before.wall_time);
    for (size_t i = 0; i < count; i++) {
        notification_release(&all[i]);
    }

    NotificationContext bump = notification_context_for_app("tactility.test", "older again");
    REQUIRE_EQ(notification_update(older, &bump, true), ERROR_NONE);
    REQUIRE_EQ(notification_get_all(all, 4, &count), ERROR_NONE);
    CHECK_EQ(all[0].id, older);
    for (size_t i = 0; i < count; i++) {
        notification_release(&all[i]);
    }
}

TEST_CASE("preloading the sources is harmless to repeat and keeps the sources") {
    ModuleFixture fixture;
    notification_preload();
    send("hello");
    notification_preload();
    NotificationSource sources[NOTIFICATION_SOURCE_LIMIT];
    size_t count = 0;
    REQUIRE_EQ(notification_get_sources(sources, NOTIFICATION_SOURCE_LIMIT, &count), ERROR_NONE);
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        found = found || std::string(sources[i].id) == "tactility.test";
    }
    CHECK(found);
}

TEST_CASE("an update event tells whether the update renotifies") {
    ModuleFixture fixture;
    TaskEventGroup group {};
    task_event_group_construct(&group);
    NotificationSubscription sub {};
    REQUIRE_EQ(notification_subscribe(&sub, &group), ERROR_NONE);
    const uint32_t id = send("first");

    NotificationContext quiet = notification_context_for_app("tactility.test", "quiet");
    REQUIRE_EQ(notification_update(id, &quiet, false), ERROR_NONE);
    NotificationContext loud = notification_context_for_app("tactility.test", "loud");
    REQUIRE_EQ(notification_update(id, &loud, true), ERROR_NONE);

    NotificationEvent event {};
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_POSTED);
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_UPDATED);
    CHECK_FALSE(event.bumped);
    REQUIRE_EQ(notification_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, NOTIFICATION_EVENT_UPDATED);
    CHECK(event.bumped);

    CHECK_EQ(notification_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("dismissing unless sticky respects the flag at the time of the call") {
    ModuleFixture fixture;
    const uint32_t plain = send("plain");
    NotificationContext context = notification_context_for_app("tactility.test", "sticky");
    notification_context_set_sticky(&context, true);
    uint32_t sticky = 0;
    REQUIRE_EQ(notification_send(&context, &sticky), ERROR_NONE);

    CHECK_EQ(notification_dismiss_unless_sticky(sticky), ERROR_NOT_ALLOWED);
    CHECK_EQ(notification_dismiss_unless_sticky(plain), ERROR_NONE);
    CHECK_EQ(notification_dismiss_unless_sticky(plain), ERROR_NOT_FOUND);

    // A sender that makes it sticky after it was read is respected
    const uint32_t later = send("later");
    NotificationContext update = notification_context_for_app("tactility.test", "later");
    notification_context_set_sticky(&update, true);
    REQUIRE_EQ(notification_update(later, &update, false), ERROR_NONE);
    CHECK_EQ(notification_dismiss_unless_sticky(later), ERROR_NOT_ALLOWED);
}

TEST_CASE("dismissing all non sticky notifications keeps the sticky ones") {
    ModuleFixture fixture;
    send("one");
    NotificationContext context = notification_context_for_app("tactility.test", "sticky");
    notification_context_set_sticky(&context, true);
    uint32_t sticky = 0;
    REQUIRE_EQ(notification_send(&context, &sticky), ERROR_NONE);
    send("two");

    notification_dismiss_non_sticky();
    CHECK_EQ(notification_get_count(), 1);
    Notification notification {};
    REQUIRE_EQ(notification_get(sticky, &notification), ERROR_NONE);
    notification_release(&notification);
}

TEST_CASE("event order increases with every event") {
    ModuleFixture fixture;
    TaskEventGroup group {};
    task_event_group_construct(&group);
    NotificationSubscription sub {};
    REQUIRE_EQ(notification_subscribe(&sub, &group), ERROR_NONE);

    const uint32_t before = notification_get_event_order();
    const uint32_t id = send("first");
    const uint32_t between = notification_get_event_order();
    REQUIRE_EQ(notification_dismiss(id), ERROR_NONE);

    NotificationEvent posted {};
    NotificationEvent dismissed {};
    REQUIRE_EQ(notification_poll(&sub, &posted), ERROR_NONE);
    REQUIRE_EQ(notification_poll(&sub, &dismissed), ERROR_NONE);
    CHECK_GT(posted.order, before);
    CHECK_EQ(posted.order, between);
    CHECK_GT(dismissed.order, between);

    CHECK_EQ(notification_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}
