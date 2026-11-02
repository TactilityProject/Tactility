// SPDX-License-Identifier: Apache-2.0
#include <app_hub/app_hub.h>
#include <app_hub/app_hub_service.h>

#include <app/manifest.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/time.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

constexpr TickType_t OPERATION_TIMEOUT = pdMS_TO_TICKS(180000);
constexpr size_t DESCRIPTION_MAX_SIZE = 2048;

static void print_usage() {
    printf(
        "usage: apphub <command>\n"
        "  refresh\n"
        "  search [query]\n"
        "  updates\n"
        "  info <id>\n"
        "  install <id>\n"
        "  update\n"
        "  remove <id>\n"
    );
}

static int report_error(const char* action, error_t error) {
    if (error == ERROR_RESOURCE_BUSY) {
        printf("apphub: %s failed: a conflicting operation is pending\n", action);
    } else {
        printf("apphub: %s failed: %s\n", action, error_to_string(error));
    }
    return 1;
}

/**
 * Queues an operation and waits for its result.
 * The subscription is made before queueing, so the operation can't finish unnoticed.
 * It times out after OPERATION_TIMEOUT without events of the operation, so a long update of many apps doesn't.
 * @param[in] finished_type the event that ends the operation and carries its result
 * @param[in] on_event called for the other events of the operation (optional)
 */
static error_t run_and_wait(
    const char* id,
    error_t (*operation)(const char* id, uint32_t* operation_id),
    AppHubEventType finished_type,
    void (*on_event)(const AppHubEvent& event) = nullptr
) {
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    AppHubEventSubscription subscription {};
    error_t error = app_hub_event_subscribe(&subscription, &event_group);
    if (error != ERROR_NONE) {
        task_event_group_destruct(&event_group);
        return error;
    }

    uint32_t operation_id = 0;
    error = operation(id, &operation_id);
    if (error == ERROR_NONE) {
        error = ERROR_TIMEOUT;
        TickType_t start = get_ticks();
        TickType_t elapsed = 0;
        bool finished = false;
        while (!finished && elapsed < OPERATION_TIMEOUT) {
            if (app_hub_event_await(&subscription, OPERATION_TIMEOUT - elapsed) == ERROR_NONE) {
                AppHubEvent event {};
                while (!finished && app_hub_event_poll(&subscription, &event) == ERROR_NONE) {
                    if (event.operation_id != operation_id) {
                        continue;
                    }
                    start = get_ticks();
                    if (event.type == finished_type) {
                        error = event.result;
                        finished = true;
                    } else if (on_event != nullptr) {
                        on_event(event);
                    }
                }
            }
            elapsed = get_ticks() - start;
        }
    }

    app_hub_event_unsubscribe(&subscription);
    task_event_group_destruct(&event_group);
    return error;
}

static error_t refresh(const char* /*id*/, uint32_t* operation_id) {
    return app_hub_refresh(operation_id);
}

/** Collects the apps first, as printing can block while app_hub_search() holds the service's lock */
static std::vector<AppHubApp> search(const char* query) {
    std::vector<AppHubApp> apps;
    app_hub_search(query, [](const AppHubApp* app, void* context) {
        static_cast<std::vector<AppHubApp>*>(context)->push_back(*app);
        return true;
    }, &apps);
    return apps;
}

static void print_app(const AppHubApp& app) {
    const char marker = app.update_available ? '^' : (app.installed ? '*' : ' ');
    const char* version = app.in_catalog ? app.version_name : app.installed_version_name;
    printf("%c %-32s %-24s %s\n", marker, app.id, app.name, version);
}

static int command_refresh() {
    printf("Downloading the catalog...\n");
    const error_t error = run_and_wait(nullptr, refresh, APP_HUB_EVENT_REFRESHED);
    if (error != ERROR_NONE) {
        return report_error("refresh", error);
    }
    printf("Catalog updated\n");
    return 0;
}

static int command_search(int argc, char* argv[], bool updates_only) {
    if (argc > 3 || (updates_only && argc > 2)) {
        print_usage();
        return 1;
    }

    const auto apps = search(argc == 3 ? argv[2] : nullptr);
    size_t count = 0;
    for (const auto& app : apps) {
        if (!updates_only || app.update_available) {
            print_app(app);
            count++;
        }
    }
    if (count == 0) {
        printf(updates_only ? "No updates available\n" : "No apps found. Run 'apphub refresh' to download the catalog.\n");
    } else if (!updates_only) {
        printf("(* = installed, ^ = update available)\n");
    }
    return 0;
}

static int command_info(int argc, char* argv[]) {
    if (argc != 3) {
        printf("usage: apphub info <id>\n");
        return 1;
    }

    AppHubApp app {};
    if (app_hub_get_app(argv[2], &app) != ERROR_NONE) {
        printf("apphub: unknown app: %s\n", argv[2]);
        return 1;
    }

    printf("id: %s\n", app.id);
    printf("name: %s\n", app.name);
    if (app.in_catalog) {
        printf("version: %s\n", app.version_name);
    }
    if (app.installed) {
        printf("installed: %s%s\n", app.installed_version_name, app.update_available ? " (update available)" : "");
    } else {
        printf("installed: no\n");
    }

    auto description = std::make_unique<char[]>(DESCRIPTION_MAX_SIZE);
    if (app_hub_get_description(app.id, description.get(), DESCRIPTION_MAX_SIZE) == ERROR_NONE && description[0] != '\0') {
        printf("\n%s\n", description.get());
    }
    return 0;
}

static int command_install(int argc, char* argv[]) {
    if (argc != 3) {
        printf("usage: apphub install <id>\n");
        return 1;
    }

    printf("Installing %s...\n", argv[2]);
    const error_t error = run_and_wait(argv[2], app_hub_install, APP_HUB_EVENT_INSTALLED);
    if (error == ERROR_NOT_FOUND) {
        printf("apphub: %s is not in the catalog\n", argv[2]);
        return 1;
    } else if (error != ERROR_NONE) {
        return report_error("install", error);
    }
    printf("Installed %s\n", argv[2]);
    return 0;
}

static error_t update_all(const char* /*id*/, uint32_t* operation_id) {
    return app_hub_update_all(operation_id);
}

static void print_update_progress(const AppHubEvent& event) {
    if (event.type == APP_HUB_EVENT_INSTALLING) {
        printf("Updating %s...\n", event.app_id);
    } else if (event.type == APP_HUB_EVENT_INSTALLED) {
        if (event.result == ERROR_NONE) {
            printf("Updated %s\n", event.app_id);
        } else {
            printf("apphub: update of %s failed: %s\n", event.app_id, error_to_string(event.result));
        }
    }
}

static int command_update(int argc) {
    if (argc != 2) {
        printf("usage: apphub update\n");
        return 1;
    }

    // Counted first, as the update reports nothing when there are no updates
    size_t count = 0;
    for (const auto& app : search(nullptr)) {
        if (app.update_available) {
            count++;
        }
    }
    if (count == 0) {
        printf("No updates available\n");
        return 0;
    }

    const error_t error = run_and_wait(nullptr, update_all, APP_HUB_EVENT_UPDATED, print_update_progress);
    if (error == ERROR_RESOURCE_BUSY || error == ERROR_TIMEOUT) {
        return report_error("update", error);
    } else if (error != ERROR_NONE) {
        printf("apphub: some apps failed to update\n");
        return 1;
    }
    printf("All apps are up to date\n");
    return 0;
}

static int command_remove(int argc, char* argv[]) {
    if (argc != 3) {
        printf("usage: apphub remove <id>\n");
        return 1;
    }

    printf("Removing %s...\n", argv[2]);
    const error_t error = run_and_wait(argv[2], app_hub_remove, APP_HUB_EVENT_REMOVED);
    if (error == ERROR_NOT_FOUND) {
        printf("apphub: %s is not installed\n", argv[2]);
        return 1;
    } else if (error != ERROR_NONE) {
        return report_error("remove", error);
    }
    printf("Removed %s\n", argv[2]);
    return 0;
}

static int32_t app_hub_command_main(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }

    const char* command = argv[1];
    if (strcmp(command, "refresh") == 0) {
        return command_refresh();
    } else if (strcmp(command, "search") == 0) {
        return command_search(argc, argv, false);
    } else if (strcmp(command, "updates") == 0) {
        return command_search(argc, argv, true);
    } else if (strcmp(command, "info") == 0) {
        return command_info(argc, argv);
    } else if (strcmp(command, "install") == 0) {
        return command_install(argc, argv);
    } else if (strcmp(command, "update") == 0) {
        return command_update(argc);
    } else if (strcmp(command, "remove") == 0) {
        return command_remove(argc, argv);
    }

    printf("apphub: unknown command: %s\n", command);
    print_usage();
    return 1;
}

extern "C" {

const AppManifest app_hub_command_manifest = {
    .id = "apphub",
    .name = "apphub",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(app_hub_command_main) },
    .flags = APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 5120, .desired_memory_capability = 0 },
};

}
