// SPDX-License-Identifier: Apache-2.0
#include <wifi/wifi_autoconnect.h>
#include <wifi/wifi_settings.h>
#include <wifi/private/wifi_service.h>

#include <app/manifest.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr size_t SCAN_RECORD_LIMIT = 32;
constexpr TickType_t SCAN_TIMEOUT = pdMS_TO_TICKS(15000);
constexpr TickType_t CONNECT_TIMEOUT = pdMS_TO_TICKS(20000);
constexpr TickType_t DISCONNECT_TIMEOUT = pdMS_TO_TICKS(5000);

static void print_usage() {
    printf(
        "usage: wifi <command>\n"
        "  status\n"
        "  radio on|off\n"
        "  scan\n"
        "  connect -s <ssid> [-p <password>] [-c <channel>] [--no-save]\n"
        "  disconnect\n"
        "  forget -s <ssid>\n"
        "  saved\n"
    );
}

static int report_error(const char* action, error_t error) {
    printf("wifi: %s failed: %s\n", action, error_to_string(error));
    return 1;
}

static const char* radio_state_to_string(WifiRadioState state) {
    switch (state) {
        case WIFI_RADIO_STATE_OFF: return "off";
        case WIFI_RADIO_STATE_ON_PENDING: return "turning on";
        case WIFI_RADIO_STATE_ON: return "on";
        case WIFI_RADIO_STATE_OFF_PENDING: return "turning off";
    }
    return "unknown";
}

static const char* station_state_to_string(WifiStationState state) {
    switch (state) {
        case WIFI_STATION_STATE_DISCONNECTED: return "disconnected";
        case WIFI_STATION_STATE_CONNECTION_PENDING: return "connecting";
        case WIFI_STATION_STATE_CONNECTED: return "connected";
    }
    return "unknown";
}

static const char* connection_error_to_string(WifiStationConnectionError error) {
    switch (error) {
        case WIFI_STATION_CONNECTION_ERROR_NONE: return "none";
        case WIFI_STATION_CONNECTION_ERROR_WRONG_CREDENTIALS: return "wrong credentials";
        case WIFI_STATION_CONNECTION_ERROR_TIMEOUT: return "timeout";
        case WIFI_STATION_CONNECTION_ERROR_TARGET_NOT_FOUND: return "access point not found";
    }
    return "unknown";
}

/**
 * Subscribes to the device's events, runs the action and waits for an event that matches.
 * The action runs after subscribing, so an event it causes can't be missed.
 * @retval ERROR_TIMEOUT no matching event arrived in time
 */
static error_t run_and_wait(
    Device* device,
    error_t (*action)(Device* device, void* context),
    void* context,
    bool (*matches)(const WifiEvent& event),
    TickType_t timeout,
    WifiEvent* out_event
) {
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    WifiEventSubscription subscription {};
    error_t error = wifi_event_subscribe(device, &subscription, &event_group);
    if (error != ERROR_NONE) {
        task_event_group_destruct(&event_group);
        return error;
    }

    error = action(device, context);
    if (error == ERROR_NONE) {
        error = ERROR_TIMEOUT;
        TickType_t start = get_ticks();
        TickType_t elapsed = 0;
        while (error == ERROR_TIMEOUT && elapsed < timeout) {
            task_event_group_wait_any(&event_group, nullptr, timeout - elapsed);
            WifiEvent event {};
            while (wifi_event_poll(&subscription, &event) == ERROR_NONE) {
                if (matches(event)) {
                    *out_event = event;
                    error = ERROR_NONE;
                    break;
                }
            }
            elapsed = get_ticks() - start;
        }
    }

    wifi_event_unsubscribe(device, &subscription);
    task_event_group_destruct(&event_group);
    return error;
}

static int command_status(Device* device) {
    WifiRadioState radio_state = WIFI_RADIO_STATE_OFF;
    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    wifi_get_radio_state(device, &radio_state);
    wifi_get_station_state(device, &station_state);

    printf("radio: %s\n", radio_state_to_string(radio_state));
    if (radio_state != WIFI_RADIO_STATE_ON) {
        return 0;
    }

    printf("station: %s\n", station_state_to_string(station_state));
    if (station_state == WIFI_STATION_STATE_DISCONNECTED) {
        return 0;
    }

    char ssid[33] = {};
    if (wifi_station_get_target_ssid(device, ssid) == ERROR_NONE) {
        printf("ssid: %s\n", ssid);
    }
    if (station_state == WIFI_STATION_STATE_CONNECTED) {
        char ip[16] = {};
        if (wifi_station_get_ipv4_address(device, ip) == ERROR_NONE) {
            printf("ip: %s\n", ip);
        }
        int32_t rssi = 0;
        if (wifi_station_get_rssi(device, &rssi) == ERROR_NONE) {
            printf("rssi: %ld dBm\n", static_cast<long>(rssi));
        }
    }
    return 0;
}

static int command_radio(Device* device, int argc, char* argv[]) {
    if (argc != 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
        printf("usage: wifi radio on|off\n");
        return 1;
    }

    bool on = strcmp(argv[2], "on") == 0;
    error_t error = on ? wifi_set_radio_on(device) : wifi_set_radio_off(device);
    if (error != ERROR_NONE) {
        return report_error(on ? "radio on" : "radio off", error);
    }
    printf("radio: %s\n", on ? "on" : "off");
    return 0;
}

static void print_scan_record(const WifiApRecord& record) {
    const char* auth = record.authentication_type == WIFI_AUTHENTICATION_TYPE_OPEN ? "open" : "secured";
    const char* saved = wifi_settings_contains(record.ssid) ? "*" : "";
    printf("%-32s %4d dBm  ch %2ld  %-7s %s\n", record.ssid, record.rssi, static_cast<long>(record.channel), auth, saved);
}

static int command_scan(Device* device) {
    error_t error = wifi_set_radio_on(device);
    if (error != ERROR_NONE) {
        return report_error("radio on", error);
    }

    WifiEvent event {};
    error = run_and_wait(
        device,
        [](Device* device, void*) {
            // A scan that is already running also ends with SCAN_FINISHED
            return wifi_is_scanning(device) ? ERROR_NONE : wifi_scan(device);
        },
        nullptr,
        [](const WifiEvent& event) { return event.type == WIFI_EVENT_TYPE_SCAN_FINISHED; },
        SCAN_TIMEOUT,
        &event
    );
    if (error != ERROR_NONE) {
        return report_error("scan", error);
    }

    WifiApRecord records[SCAN_RECORD_LIMIT];
    size_t count = SCAN_RECORD_LIMIT;
    error = wifi_get_scan_results(device, records, &count);
    if (error != ERROR_NONE) {
        return report_error("scan", error);
    }

    if (count == 0) {
        printf("No access points found\n");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        print_scan_record(records[i]);
    }
    printf("(* = saved)\n");
    return 0;
}

static error_t disconnect_and_wait(Device* device) {
    WifiEvent event {};
    return run_and_wait(
        device,
        [](Device* device, void*) { return wifi_station_disconnect(device); },
        nullptr,
        [](const WifiEvent& event) {
            return event.type == WIFI_EVENT_TYPE_STATION_STATE_CHANGED && event.station_state == WIFI_STATION_STATE_DISCONNECTED;
        },
        DISCONNECT_TIMEOUT,
        &event
    );
}

static int command_connect(Device* device, int argc, char* argv[]) {
    const char* ssid = nullptr;
    const char* password = nullptr;
    const char* channel = nullptr;
    bool save = true;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            ssid = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            channel = argv[++i];
        } else if (strcmp(argv[i], "--no-save") == 0) {
            save = false;
        } else {
            ssid = nullptr;
            break;
        }
    }

    if (ssid == nullptr) {
        printf("usage: wifi connect -s <ssid> [-p <password>] [-c <channel>] [--no-save]\n");
        return 1;
    }
    if (strlen(ssid) == 0 || strlen(ssid) > WIFI_SETTINGS_SSID_LIMIT) {
        printf("wifi: SSID must be 1 to %d characters\n", WIFI_SETTINGS_SSID_LIMIT);
        return 1;
    }
    if (password != nullptr && strlen(password) > WIFI_SETTINGS_PASSWORD_LIMIT) {
        printf("wifi: password must be at most %d characters\n", WIFI_SETTINGS_PASSWORD_LIMIT);
        return 1;
    }

    WifiApSettings settings {};
    bool from_saved_settings = password == nullptr && wifi_settings_load(ssid, &settings) == ERROR_NONE;
    if (!from_saved_settings) {
        strcpy(settings.ssid, ssid);
        strcpy(settings.password, password != nullptr ? password : "");
        settings.auto_connect = WIFI_SETTINGS_AUTO_CONNECT_DEFAULT;
        settings.channel = 0;
    }
    if (channel != nullptr) {
        settings.channel = static_cast<int32_t>(strtol(channel, nullptr, 10));
    }

    error_t error = wifi_set_radio_on(device);
    if (error != ERROR_NONE) {
        return report_error("radio on", error);
    }

    // Keeps auto-connect out of the way until this attempt finishes
    wifi_autoconnect_pause_until_connected();

    // Disconnecting first keeps the old connection's disconnect from being reported as the result of this attempt
    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    wifi_get_station_state(device, &station_state);
    if (station_state != WIFI_STATION_STATE_DISCONNECTED) {
        error = disconnect_and_wait(device);
        if (error != ERROR_NONE) {
            return report_error("disconnect", error);
        }
    }

    printf("Connecting to %s...\n", settings.ssid);
    WifiEvent event {};
    error = run_and_wait(
        device,
        [](Device* device, void* context) {
            auto* settings = static_cast<const WifiApSettings*>(context);
            return wifi_station_connect(device, settings->ssid, settings->password, settings->channel);
        },
        &settings,
        [](const WifiEvent& event) { return event.type == WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT; },
        CONNECT_TIMEOUT,
        &event
    );
    if (error != ERROR_NONE) {
        return report_error("connect", error);
    }
    if (event.connection_error != WIFI_STATION_CONNECTION_ERROR_NONE) {
        printf("wifi: connect failed: %s\n", connection_error_to_string(event.connection_error));
        return 1;
    }

    printf("Connected to %s\n", settings.ssid);
    if (save && !from_saved_settings) {
        error = wifi_settings_save(&settings);
        if (error != ERROR_NONE) {
            return report_error("saving credentials", error);
        }
        printf("Credentials saved\n");
    }
    return 0;
}

static int command_disconnect(Device* device) {
    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    wifi_get_station_state(device, &station_state);
    if (station_state == WIFI_STATION_STATE_DISCONNECTED) {
        printf("Not connected\n");
        return 0;
    }

    // Keeps auto-connect from immediately reconnecting
    wifi_autoconnect_pause_until_connected();
    error_t error = disconnect_and_wait(device);
    if (error != ERROR_NONE) {
        return report_error("disconnect", error);
    }
    printf("Disconnected\n");
    return 0;
}

static int command_forget(int argc, char* argv[]) {
    if (argc != 4 || strcmp(argv[2], "-s") != 0) {
        printf("usage: wifi forget -s <ssid>\n");
        return 1;
    }

    error_t error = wifi_settings_remove(argv[3]);
    if (error == ERROR_NOT_FOUND) {
        printf("wifi: %s is not saved\n", argv[3]);
        return 1;
    } else if (error != ERROR_NONE) {
        return report_error("forget", error);
    }
    printf("Removed %s\n", argv[3]);
    return 0;
}

static int command_saved() {
    size_t count = 0;
    wifi_settings_for_each(&count, [](const char* ssid, void* context) {
        printf("%s\n", ssid);
        (*static_cast<size_t*>(context))++;
        return true;
    });
    if (count == 0) {
        printf("No saved access points\n");
    }
    return 0;
}

static int run_device_command(const char* command, int argc, char* argv[]) {
    Device* device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &device) != ERROR_NONE) {
        printf("wifi: No WiFi device found\n");
        return 1;
    }

    int result;
    if (strcmp(command, "status") == 0) {
        result = command_status(device);
    } else if (strcmp(command, "radio") == 0) {
        result = command_radio(device, argc, argv);
    } else if (strcmp(command, "scan") == 0) {
        result = command_scan(device);
    } else if (strcmp(command, "connect") == 0) {
        result = command_connect(device, argc, argv);
    } else {
        result = command_disconnect(device);
    }

    device_put(device);
    return result;
}

static int32_t wifi_command_main(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }

    const char* command = argv[1];
    if (strcmp(command, "forget") == 0) {
        return command_forget(argc, argv);
    } else if (strcmp(command, "saved") == 0) {
        return command_saved();
    } else if (
        strcmp(command, "status") == 0 ||
        strcmp(command, "radio") == 0 ||
        strcmp(command, "scan") == 0 ||
        strcmp(command, "connect") == 0 ||
        strcmp(command, "disconnect") == 0
    ) {
        return run_device_command(command, argc, argv);
    }

    printf("wifi: unknown command: %s\n", command);
    print_usage();
    return 1;
}

extern "C" {

const AppManifest wifi_command_manifest = {
    .id = "wifi",
    .name = "wifi",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(wifi_command_main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 5120, .desired_memory_capability = 0 },
};

}
