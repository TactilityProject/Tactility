#include "doctest.h"

#include <lvgl/module.h>
#include <lvgl/lvgl.h>
#include <lvgl_window_manager/dialog.h>
#include <lvgl_window_manager/window_manager.h>
#include <tactility/concurrent/thread.h>
#include <tactility/delay.h>
#include <tactility/module.h>
#include "window_manager_internal.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef DIALOG_START_WRAPPING
namespace {
enum class StartMode { Normal, Pause, FailAlloc, FailStart };
std::atomic<StartMode> start_mode { StartMode::Normal };
std::atomic<bool> allocation_entered { false };
std::atomic<bool> allocation_release { false };
std::atomic<Thread*> fail_start_thread { nullptr };
}

extern "C" Thread* __real_thread_alloc_full(const char*, size_t, thread_main_fn_t, void*, portBASE_TYPE);
extern "C" error_t __real_thread_start(Thread*);

extern "C" Thread* __wrap_thread_alloc_full(const char* name, size_t stack_size, thread_main_fn_t function, void* context, portBASE_TYPE affinity) {
    const StartMode mode = start_mode.load();
    if (std::strcmp(name, "dialog") == 0 && mode != StartMode::Normal) {
        allocation_entered.store(true);
        while (!allocation_release.load()) {
            delay_millis(5);
        }
        if (mode == StartMode::FailAlloc) {
            return nullptr;
        }
        Thread* thread = __real_thread_alloc_full(name, stack_size, function, context, affinity);
        if (mode == StartMode::FailStart) {
            fail_start_thread.store(thread);
        }
        return thread;
    }
    return __real_thread_alloc_full(name, stack_size, function, context, affinity);
}

extern "C" error_t __wrap_thread_start(Thread* thread) {
    if (thread == fail_start_thread.load() && thread != nullptr) {
        fail_start_thread.store(nullptr);
        return ERROR_UNDEFINED;
    }
    return __real_thread_start(thread);
}
#endif

namespace {

std::atomic<bool> display_ready { false };

void on_lvgl_start() {
    display_ready.store(lv_display_create(320, 240) != nullptr, std::memory_order_release);
}

struct CallbackContext {
    std::atomic<bool> started { false };
    std::atomic<bool> release { false };
    std::atomic<bool> finished { false };
    std::atomic<int> observedMarker { 0 };
    int marker = 0x5a17;
};

void on_dialog_result(int32_t, void* user_data) {
    auto* context = static_cast<CallbackContext*>(user_data);
    context->started.store(true, std::memory_order_release);
    while (!context->release.load(std::memory_order_acquire)) {
        delay_millis(5);
    }
    context->observedMarker.store(context->marker, std::memory_order_release);
    context->finished.store(true, std::memory_order_release);
}

struct RemovalRequest {
    WindowId window_id = 0;
    std::atomic<bool> done { false };
};

#ifdef DIALOG_START_WRAPPING
struct DialogRequest {
    AppInstanceId app_instance_id = 0;
    CallbackContext* callback = nullptr;
    error_t result = ERROR_UNDEFINED;
};

int32_t start_dialog_request(void* user_data) {
    auto* request = static_cast<DialogRequest*>(user_data);
    const char* labels[] = { "OK" };
    request->result = window_manager_alert_dialog(request->app_instance_id, "Test", "Starting", labels, 1, on_dialog_result, request->callback);
    return 0;
}
#endif

int32_t remove_window(void* user_data) {
    auto* request = static_cast<RemovalRequest*>(user_data);
    window_manager_remove(request->window_id);
    request->done.store(true, std::memory_order_release);
    return 0;
}

bool wait_for(const std::atomic<bool>& value, uint32_t timeout_ms) {
    for (uint32_t elapsed_ms = 0; elapsed_ms < timeout_ms; elapsed_ms += 5) {
        if (value.load(std::memory_order_acquire)) {
            return true;
        }
        delay_millis(5);
    }
    return value.load(std::memory_order_acquire);
}

struct Fixture {
    CallbackContext callback;
    RemovalRequest removal;
    Thread* remover = nullptr;
#ifdef DIALOG_START_WRAPPING
    DialogRequest dialog_request;
    Thread* starter = nullptr;
#endif
    std::vector<WindowId> windows;
    bool lvgl_started = false;
    bool window_manager_started = false;

    bool start() {
        display_ready.store(false, std::memory_order_release);
        lvgl_module_configure({
            .on_start = on_lvgl_start,
            .on_stop = nullptr,
            .task_priority = 1,
            .task_stack_size = 8192,
        });
        if (lvgl_module.start() != ERROR_NONE) {
            return false;
        }
        lvgl_started = true;
        if (!wait_for(display_ready, 5000)) {
            return false;
        }
        if (window_manager_start() != ERROR_NONE) {
            return false;
        }
        window_manager_started = true;
        return true;
    }

    WindowId create_window(AppInstanceId app_instance_id) {
        const WindowId id = window_manager_create(app_instance_id, nullptr, nullptr);
        if (id != 0) {
            windows.push_back(id);
        }
        return id;
    }

    bool show_blocking_dialog(AppInstanceId app_instance_id) {
        const char* button_labels[] = { "OK" };
        if (window_manager_alert_dialog(
                app_instance_id,
                "Test",
                "Wait for removal",
                button_labels,
                1,
                on_dialog_result,
                &callback
            ) != ERROR_NONE) {
            return false;
        }
        window_manager_dialog_dismiss();
        return wait_for(callback.started, 5000);
    }

    bool start_removal(WindowId window_id) {
        removal.window_id = window_id;
        removal.done.store(false, std::memory_order_release);
        remover = thread_alloc_full("window-removal", 4096, remove_window, &removal, -1);
        if (remover == nullptr) {
            return false;
        }
        if (thread_start(remover) != ERROR_NONE) {
            thread_free(remover);
            remover = nullptr;
            return false;
        }
        return true;
    }

    error_t join_removal() {
        if (remover == nullptr) {
            return ERROR_INVALID_STATE;
        }
        const error_t result = thread_join(remover, pdMS_TO_TICKS(10000), pdMS_TO_TICKS(5));
        if (result == ERROR_NONE) {
            thread_free(remover);
            remover = nullptr;
        }
        return result;
    }

    ~Fixture() {
        callback.release.store(true, std::memory_order_release);
#ifdef DIALOG_START_WRAPPING
        allocation_release.store(true);
        if (starter != nullptr && thread_join(starter, pdMS_TO_TICKS(10000), pdMS_TO_TICKS(5)) == ERROR_NONE) {
            thread_free(starter);
        }
        start_mode.store(StartMode::Normal);
#endif
        if (remover != nullptr) {
            if (thread_join(remover, pdMS_TO_TICKS(10000), pdMS_TO_TICKS(5)) == ERROR_NONE) {
                thread_free(remover);
            }
        }
        if (window_manager_started) {
            for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                window_manager_remove(*it);
            }
            window_manager_stop();
        }
        if (lvgl_started) {
            delay_millis(20);
            lvgl_module.stop();
        }
    }
};

} // namespace

TEST_CASE("window removal waits past the dialog drain timeout") {
    Fixture fixture;
    REQUIRE(fixture.start());

    const WindowId window_id = fixture.create_window(101);
    REQUIRE_NE(window_id, 0);
    REQUIRE(fixture.show_blocking_dialog(101));
    REQUIRE(fixture.start_removal(window_id));

    delay_millis(3250);
    CHECK_FALSE(fixture.removal.done.load(std::memory_order_acquire));

    fixture.callback.release.store(true, std::memory_order_release);
    CHECK_EQ(fixture.join_removal(), ERROR_NONE);
    CHECK(fixture.callback.finished.load(std::memory_order_acquire));
    CHECK_EQ(fixture.callback.observedMarker.load(std::memory_order_acquire), fixture.callback.marker);
}

TEST_CASE("removing a buried window drains its app dialog callback") {
    Fixture fixture;
    REQUIRE(fixture.start());

    const WindowId app_window = fixture.create_window(201);
    REQUIRE_NE(app_window, 0);
    REQUIRE(fixture.show_blocking_dialog(201));
    REQUIRE_NE(fixture.create_window(202), 0);
    REQUIRE(fixture.start_removal(app_window));

    delay_millis(50);
    CHECK_FALSE(fixture.removal.done.load(std::memory_order_acquire));

    fixture.callback.release.store(true, std::memory_order_release);
    CHECK_EQ(fixture.join_removal(), ERROR_NONE);
    CHECK(fixture.callback.finished.load(std::memory_order_acquire));
    CHECK_EQ(fixture.callback.observedMarker.load(std::memory_order_acquire), fixture.callback.marker);
}

TEST_CASE("removal rejects new dialogs until an app's callbacks drain") {
    Fixture fixture;
    REQUIRE(fixture.start());

    const WindowId buried_window = fixture.create_window(251);
    REQUIRE_NE(buried_window, 0);
    REQUIRE(fixture.show_blocking_dialog(251));
    REQUIRE_NE(fixture.create_window(251), 0);
    REQUIRE(fixture.start_removal(buried_window));
    delay_millis(100);
    CHECK_FALSE(fixture.removal.done.load());

    const char* labels[] = { "OK" };
    CHECK_EQ(window_manager_alert_dialog(251, "Test", "Removing", labels, 1, nullptr, nullptr), ERROR_INVALID_STATE);
    fixture.callback.release.store(true);
    REQUIRE_EQ(fixture.join_removal(), ERROR_NONE);
    CHECK_EQ(window_manager_alert_dialog(251, "Test", "Ready", labels, 1, nullptr, nullptr), ERROR_NONE);
    window_manager_dialog_dismiss();
}

TEST_CASE("final window removal releases the app cancellation record") {
    Fixture fixture;
    REQUIRE(fixture.start());

    const WindowId window_id = fixture.create_window(261);
    REQUIRE_NE(window_id, 0);
    window_manager_remove(window_id);

    CHECK_FALSE(dialog_is_app_canceled(261));
    const char* labels[] = { "OK" };
    CHECK_EQ(window_manager_alert_dialog(261, "Test", "No window", labels, 1, nullptr, nullptr), ERROR_INVALID_STATE);
}

TEST_CASE("cancellation records survive overlapping removals and are isolated by app") {
    Fixture fixture;
    REQUIRE(fixture.start());
    REQUIRE_NE(fixture.create_window(271), 0);
    REQUIRE_NE(fixture.create_window(272), 0);

    dialog_cancel_for_app(271);
    dialog_cancel_for_app(271);
    dialog_cancel_for_app(272);

    CHECK(dialog_is_app_canceled(271));
    CHECK(dialog_is_app_canceled(272));
    dialog_finish_remove_for_app(271);
    CHECK(dialog_is_app_canceled(271));
    dialog_finish_remove_for_app(272);
    CHECK_FALSE(dialog_is_app_canceled(272));
    CHECK(dialog_is_app_canceled(271));
    dialog_finish_remove_for_app(271);
    CHECK_FALSE(dialog_is_app_canceled(271));
}

#ifdef DIALOG_START_WRAPPING
TEST_CASE("dialog task startup failures release the modal slot") {
    Fixture fixture;
    REQUIRE(fixture.start());
    REQUIRE_NE(fixture.create_window(351), 0);
    const char* labels[] = { "OK" };

    for (StartMode mode : { StartMode::FailAlloc, StartMode::FailStart }) {
        allocation_release.store(true);
        start_mode.store(mode);
        CHECK_EQ(window_manager_alert_dialog(351, "Test", "Failure", labels, 1, nullptr, nullptr), ERROR_OUT_OF_MEMORY);
        start_mode.store(StartMode::Normal);
        CHECK_EQ(window_manager_alert_dialog(351, "Test", "Retry", labels, 1, nullptr, nullptr), ERROR_NONE);
        window_manager_dialog_dismiss();
    }
}

TEST_CASE("removal tracks a dialog admitted before its task starts") {
    for (StartMode mode : { StartMode::Pause, StartMode::FailAlloc, StartMode::FailStart }) {
        Fixture fixture;
        REQUIRE(fixture.start());
        const WindowId window_id = fixture.create_window(301);
        REQUIRE_NE(window_id, 0);

        allocation_entered.store(false);
        allocation_release.store(false);
        start_mode.store(mode);
        fixture.dialog_request = { 301, &fixture.callback, ERROR_UNDEFINED };
        fixture.starter = thread_alloc_full("dialog-starter", 4096, start_dialog_request, &fixture.dialog_request, -1);
        REQUIRE(fixture.starter != nullptr);
        REQUIRE_EQ(thread_start(fixture.starter), ERROR_NONE);
        REQUIRE(wait_for(allocation_entered, 5000));
        REQUIRE(fixture.start_removal(window_id));

        for (int i = 0; i < 1000 && window_manager_get_state(window_id) != WINDOW_STATE_REVOKED; ++i) {
            delay_millis(5);
        }
        CHECK_EQ(window_manager_get_state(window_id), WINDOW_STATE_REVOKED);
        delay_millis(100);
        CHECK_FALSE(fixture.removal.done.load());

        allocation_release.store(true);
        REQUIRE_EQ(thread_join(fixture.starter, pdMS_TO_TICKS(10000), pdMS_TO_TICKS(5)), ERROR_NONE);
        thread_free(fixture.starter);
        fixture.starter = nullptr;
        CHECK_EQ(fixture.dialog_request.result, mode == StartMode::Pause ? ERROR_NONE : ERROR_OUT_OF_MEMORY);
        CHECK_EQ(fixture.join_removal(), ERROR_NONE);
        CHECK_FALSE(fixture.callback.started.load());

        start_mode.store(StartMode::Normal);
        REQUIRE_NE(fixture.create_window(302), 0);
        REQUIRE(fixture.show_blocking_dialog(302));
        fixture.callback.release.store(true);
        REQUIRE(wait_for(fixture.callback.finished, 5000));
    }
}
#endif