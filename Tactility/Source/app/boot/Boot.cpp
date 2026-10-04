#include <Tactility/app/boot/BootSequence.h>

#include <tactility/check.h>
#include <tactility/memory.h>
#include <tactility/time.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

namespace tt::app::boot {

extern const ::AppManifest manifest;

namespace {

int32_t appMain(int argc, char* argv[]) {
    const auto start_time = get_ticks();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    if (runBootSequence(start_time)) {
        app_event_emit_close(app_scheduler_current_app_id());
    }

    // Waits until app_start(launcher) (or a permanent stop) tells us to give up -
    // startNextApp() above is what triggers that, via app-module's "save the previously active
    // app" policy. When the boot halted on an error screen, this waits forever.
    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        bool shouldClose = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
        if (shouldClose) break;
    }

    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.boot",
    .name = "Boot",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 6144, .desired_memory_capability = MEMORY_CAPABILITY_INTERNAL }
};

} // namespace
