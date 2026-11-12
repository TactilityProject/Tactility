// SPDX-License-Identifier: Apache-2.0
#include <mesh/mesh.h>
#include <mesh/mesh_service.h>

#include <app/event.h>
#include <app/io.h>
#include <app/manifest.h>

#include <tactility/Memory.hpp>
#include <tactility/std/map.hpp>
#include <tactility/std/vector.hpp>

#include <tactility/concurrent/task_event_group.h>

#include <tactility/drivers/radio.h>
#include <tactility/radio_settings.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

constexpr size_t CHANNEL_LIMIT = 8;
constexpr size_t INSPECT_DEFAULT_AMOUNT = 20;
constexpr size_t INSPECT_MAX_AMOUNT = 200;
constexpr char CTRL_C = 0x03;

static void print_usage() {
    printf(
        "usage: mesh <command>\n"
        "  status\n"
        "  on|off\n"
        "  backend [<id>]\n"
        "  region [<name>]\n"
        "  preset [<name>]\n"
        "  channels\n"
        "  join <#channel>\n"
        "  inspect [amount] [-f|--follow]\n"
    );
}

static int report_error(const char* action, error_t error) {
    printf("mesh: %s failed: %s\n", action, error_to_string(error));
    return 1;
}

static int command_status() {
    printf("enabled: %s\n", mesh_is_enabled() ? "yes" : "no");
    printf("state: %s\n", mesh_state_to_string(mesh_get_state()));

    char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
    if (mesh_get_backend(backend, sizeof(backend)) == ERROR_NONE) {
        printf("backend: %s\n", backend);
    }
    char preset[MESH_NAME_MAX_LENGTH + 1];
    if (mesh_get_preset(nullptr, preset, sizeof(preset)) == ERROR_NONE) {
        printf("preset: %s\n", preset);
    }
    printf("region: %s\n", radio_region_to_string(radio_settings_load_region()));
    return 0;
}

static int command_enable(bool enabled) {
    error_t error = mesh_set_enabled(enabled);
    if (error != ERROR_NONE) {
        return report_error(enabled ? "on" : "off", error);
    }
    printf("state: %s\n", mesh_state_to_string(mesh_get_state()));
    return 0;
}

static int command_backend(int argc, char* argv[]) {
    if (argc > 3) {
        printf("usage: mesh backend [<id>]\n");
        return 1;
    }

    if (argc == 3) {
        error_t error = mesh_set_backend(argv[2]);
        if (error == ERROR_NOT_FOUND) {
            printf("mesh: unknown backend: %s\n", argv[2]);
            return 1;
        } else if (error != ERROR_NONE) {
            return report_error("backend", error);
        }
        printf("backend: %s\n", argv[2]);
        return 0;
    }

    char selected[MESH_BACKEND_ID_MAX_LENGTH + 1] = {};
    mesh_get_backend(selected, sizeof(selected));
    const size_t count = mesh_get_backend_count();
    if (count == 0) {
        printf("No backends available\n");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        char id[MESH_BACKEND_ID_MAX_LENGTH + 1];
        if (mesh_get_backend_id(i, id, sizeof(id)) == ERROR_NONE) {
            printf("%s %s\n", strcmp(id, selected) == 0 ? "*" : " ", id);
        }
    }
    return 0;
}

static int command_region(int argc, char* argv[]) {
    if (argc > 3) {
        printf("usage: mesh region [<name>]\n");
        return 1;
    }

    if (argc == 3) {
        RadioRegion region = RADIO_REGION_UNSET;
        if (radio_region_from_string(argv[2], &region) != ERROR_NONE || region == RADIO_REGION_UNSET) {
            printf("mesh: unknown region: %s\n", argv[2]);
            return 1;
        }
        error_t error = radio_settings_save_region(region);
        if (error != ERROR_NONE) {
            return report_error("region", error);
        }
        printf("region: %s\n", argv[2]);
        return 0;
    }

    const RadioRegion selected = radio_settings_load_region();
    for (int i = RADIO_REGION_UNSET + 1; i < RADIO_REGION_COUNT; i++) {
        const auto region = static_cast<RadioRegion>(i);
        printf("%s %s\n", region == selected ? "*" : " ", radio_region_to_string(region));
    }
    return 0;
}

static int command_preset(int argc, char* argv[]) {
    if (argc > 3) {
        printf("usage: mesh preset [<name>]\n");
        return 1;
    }

    if (argc == 3) {
        // A preset can need another region. It's saved first, and the preset isn't set when that fails.
        RadioRegion region = RADIO_REGION_UNSET;
        if (mesh_get_preset_region(nullptr, argv[2], &region) == ERROR_NONE && region != RADIO_REGION_UNSET && region != radio_settings_load_region()) {
            const error_t region_error = radio_settings_save_region(region);
            if (region_error != ERROR_NONE) {
                return report_error("region", region_error);
            }
        }
        error_t error = mesh_set_preset(nullptr, argv[2]);
        if (error == ERROR_NOT_FOUND) {
            printf("mesh: unknown preset: %s\n", argv[2]);
            return 1;
        } else if (error != ERROR_NONE) {
            return report_error("preset", error);
        }
        printf("preset: %s\n", argv[2]);
        return 0;
    }

    char selected[MESH_NAME_MAX_LENGTH + 1] = {};
    mesh_get_preset(nullptr, selected, sizeof(selected));
    const size_t count = mesh_get_preset_count(nullptr);
    if (count == 0) {
        printf("The backend has no presets\n");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        char name[MESH_NAME_MAX_LENGTH + 1];
        if (mesh_get_preset_name(nullptr, i, name, sizeof(name)) == ERROR_NONE) {
            printf("%s %s\n", strcmp(name, selected) == 0 ? "*" : " ", name);
        }
    }
    return 0;
}

static int command_channels() {
    MeshChannel channels[CHANNEL_LIMIT];
    size_t count = 0;
    error_t error = mesh_get_channels(channels, CHANNEL_LIMIT, &count);
    if (error == ERROR_NOT_SUPPORTED) {
        printf("The backend has no channels\n");
        return 0;
    } else if (error != ERROR_NONE) {
        return report_error("channels", error);
    }

    if (count == 0) {
        printf("No channels\n");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        printf("%u %s\n", channels[i].index, channels[i].name);
    }
    return 0;
}

static int command_join(int argc, char* argv[]) {
    const char* name = argc == 3 ? argv[2] : nullptr;
    if (name == nullptr || name[0] != '#' || name[1] == '\0') {
        printf("usage: mesh join <#channel>\n");
        return 1;
    }
    if (strlen(name) > MESH_NAME_MAX_LENGTH) {
        printf("mesh: channel name must be at most %d characters\n", MESH_NAME_MAX_LENGTH);
        return 1;
    }

    // A new channel gets the next free index
    MeshChannel channels[CHANNEL_LIMIT];
    size_t count = 0;
    uint8_t index = 0;
    bool found = false;
    if (mesh_get_channels(channels, CHANNEL_LIMIT, &count) == ERROR_NONE) {
        for (size_t i = 0; i < count; i++) {
            if (strcmp(channels[i].name, name) == 0) {
                index = channels[i].index;
                found = true;
                break;
            }
            index = std::max<uint8_t>(index, channels[i].index + 1);
        }
        if (!found && count >= CHANNEL_LIMIT) {
            printf("mesh: no free channel slot\n");
            return 1;
        }
    }

    MeshChannel channel {};
    channel.index = index;
    strncpy(channel.name, name, MESH_NAME_MAX_LENGTH);
    channel.key_length = 0;
    error_t error = mesh_set_channel(&channel);
    if (error != ERROR_NONE) {
        return report_error("join", error);
    }
    printf("Joined %s\n", name);
    return 0;
}

static void inspect_format_address(const MeshAddress& address, char* out, size_t size) {
    // The first bytes identify the node well enough to tell nodes apart
    out[0] = '\0';
    for (size_t i = 0; i < address.length && i < 4 && strlen(out) + 2 < size; i++) {
        snprintf(out + strlen(out), size - strlen(out), "%02x", address.bytes[i]);
    }
}

static void inspect_format_signal(float rssi, float snr, char* out, size_t size) {
    if (std::isnan(snr)) {
        snprintf(out, size, "%.0f dBm", rssi);
    } else {
        snprintf(out, size, "%.0f dBm, %.1f dB", rssi, snr);
    }
}

static const char* inspect_delivery_text(MeshDeliveryState delivery) {
    switch (delivery) {
        case MESH_DELIVERY_QUEUED: return "queued";
        case MESH_DELIVERY_BLOCKED: return "blocked";
        case MESH_DELIVERY_SENT: return "sent";
        case MESH_DELIVERY_DELIVERED: return "delivered";
        case MESH_DELIVERY_FAILED: return "failed";
        case MESH_DELIVERY_CANCELLED: return "cancelled";
        default: return "";
    }
}

static void inspect_load_channel_names(tt::std::map<uint8_t, MeshChannel>& names) {
    MeshChannel channels[CHANNEL_LIMIT];
    size_t count = 0;
    if (mesh_get_channels(channels, CHANNEL_LIMIT, &count) != ERROR_NONE) {
        return;
    }
    names.clear();
    for (size_t i = 0; i < count; i++) {
        names[channels[i].index] = channels[i];
    }
}

static void inspect_print_message(const tt::std::map<uint8_t, MeshChannel>& channels, const MeshMessage& message) {
    char target[MESH_NAME_MAX_LENGTH + 16];
    if (message.peer.length == 0) {
        const auto channel = channels.find(message.channel);
        if (channel != channels.end()) {
            snprintf(target, sizeof(target), "%s", channel->second.name);
        } else {
            snprintf(target, sizeof(target), "channel %u", (unsigned)message.channel);
        }
    } else {
        inspect_format_address(message.peer, target, sizeof(target));
    }
    if (message.outgoing) {
        printf("%s > %s [%s]: %s\n", message.backend, target, inspect_delivery_text(message.delivery), message.text);
    } else {
        char signal[40];
        inspect_format_signal(message.rssi, message.snr, signal, sizeof(signal));
        printf("%s < %s %s (%s): %s\n", message.backend, message.sender_name[0] != '\0' ? message.sender_name : "?", target, signal, message.text);
    }
    fflush(stdout);
}

static void inspect_print_frame(const MeshEvent& event) {
    char backend[MESH_BACKEND_ID_MAX_LENGTH + 1] = "-";
    mesh_get_backend(backend, sizeof(backend));
    char signal[40];
    inspect_format_signal(event.frame.rssi, event.frame.snr, signal, sizeof(signal));
    if (event.frame.kind == MESH_FRAME_ADVERT) {
        char from[16];
        inspect_format_address(event.frame.from, from, sizeof(from));
        printf("%s < advert %s (%s): %s\n", backend, from, signal, event.frame.name[0] != '\0' ? event.frame.name : "[no name]");
    } else {
        printf("%s < %u bytes (%s): [could not read]\n", backend, (unsigned)event.frame.length, signal);
    }
    fflush(stdout);
}

static void inspect_print_message_by_seq(const tt::std::map<uint8_t, MeshChannel>& channels, uint32_t seq) {
    auto* message = tt::newObject<MeshMessage>();
    if (message == nullptr) {
        return;
    }
    if (mesh_message_get(seq, message) == ERROR_NONE) {
        inspect_print_message(channels, *message);
    }
    tt::deleteObject(message);
}

/** Prints the most recent messages, oldest first */
/** @return the seq of the newest printed message, 0 when there is none */
static uint32_t inspect_print_history(const tt::std::map<uint8_t, MeshChannel>& channels, size_t amount) {
    tt::std::vector<MeshMessage> history(amount);
    size_t count = 0;
    if (mesh_history_get(0, history.data(), amount, &count) != ERROR_NONE) {
        return 0;
    }
    for (size_t i = count; i > 0; i--) {
        inspect_print_message(channels, history[i - 1]);
    }
    return count > 0 ? history[0].seq : 0;
}

/** @return the key that asks to stop ('q' or Ctrl+C, which arrives as a byte on stdin), or 0 when none was typed */
static char inspect_stop_key_typed() {
    char typed = 0;
    char byte = 0;
    while (app_io_await(STDIN_FILENO, APP_FILE_WAIT_READABLE, 0) == ERROR_NONE && app_io_read(STDIN_FILENO, &byte, 1) == 1) {
        if (byte == CTRL_C || byte == 'q' || byte == 'Q') {
            typed = byte;
        }
    }
    return typed;
}

static int command_inspect(int argc, char* argv[]) {
    size_t amount = INSPECT_DEFAULT_AMOUNT;
    bool follow = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--follow") == 0) {
            follow = true;
            continue;
        }
        char* end = nullptr;
        const unsigned long value = strtoul(argv[i], &end, 10);
        if (argv[i][0] < '0' || argv[i][0] > '9' || end == argv[i] || *end != '\0' || value == 0) {
            printf("usage: mesh inspect [amount] [-f|--follow]\n");
            return 1;
        }
        amount = std::min<size_t>(value, INSPECT_MAX_AMOUNT);
    }

    if (!follow) {
        tt::std::map<uint8_t, MeshChannel> channels;
        inspect_load_channel_names(channels);
        inspect_print_history(channels, amount);
        return 0;
    }

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    AppEventSubscription appEvents {};
    MeshEventSubscription meshEvents {};
    if (app_event_subscribe(&appEvents, &eventGroup) != ERROR_NONE) {
        task_event_group_destruct(&eventGroup);
        return report_error("inspect", ERROR_RESOURCE);
    }
    // Subscribed before the history is read, so no message falls between the two
    const error_t subscribed = mesh_event_subscribe(&meshEvents, &eventGroup);
    if (subscribed != ERROR_NONE) {
        app_event_unsubscribe(&appEvents);
        task_event_group_destruct(&eventGroup);
        return report_error("inspect", subscribed);
    }

    printf("state: %s (Ctrl+C or q to stop)\n", mesh_state_to_string(mesh_get_state()));
    fflush(stdout);
    tt::std::map<uint8_t, MeshChannel> channels;
    inspect_load_channel_names(channels);
    uint32_t lastSeq = inspect_print_history(channels, amount);
    const char* stopReason = nullptr;
    bool stop = false;
    while (!stop) {
        // Wakes up on events, and regularly to look for Ctrl+C
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, pdMS_TO_TICKS(100));
        const char key = inspect_stop_key_typed();
        if (key != 0) {
            stop = true;
            stopReason = key == CTRL_C ? "Ctrl+C" : "q";
        }

        MeshEvent meshEvent {};
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            switch (meshEvent.type) {
                case MESH_EVENT_MESSAGE:
                    // The history may hold a message that also arrived as an event
                    if (meshEvent.seq > lastSeq) {
                        inspect_print_message_by_seq(channels, meshEvent.seq);
                        lastSeq = meshEvent.seq;
                    }
                    break;
                case MESH_EVENT_FRAME:
                    inspect_print_frame(meshEvent);
                    break;
                case MESH_EVENT_DELIVERY:
                    printf("#%u %s\n", (unsigned)meshEvent.seq, inspect_delivery_text(meshEvent.delivery));
                    fflush(stdout);
                    break;
                case MESH_EVENT_STATE:
                    inspect_load_channel_names(channels);
                    printf("state: %s\n", mesh_state_to_string(meshEvent.state));
                    fflush(stdout);
                    break;
                case MESH_EVENT_CHANNEL:
                    inspect_load_channel_names(channels);
                    break;
                default:
                    break;
            }
        }

        AppEvent appEvent {};
        while (app_event_poll(&appEvents, &appEvent) == ERROR_NONE) {
            // SIGTERM arrives as a close event, and Ctrl+C from a shell that sends signals as SIGINT
            if (appEvent.type == APP_EVENT_CLOSE) {
                stop = true;
                stopReason = "closed";
            } else if (appEvent.type == APP_EVENT_SIGNAL && (appEvent.signal.sig == SIGINT || appEvent.signal.sig == SIGHUP)) {
                stop = true;
                stopReason = appEvent.signal.sig == SIGINT ? "SIGINT" : "SIGHUP";
            }
        }
    }

    printf("stopped: %s\n", stopReason);
    mesh_event_unsubscribe(&meshEvents);
    app_event_unsubscribe(&appEvents);
    task_event_group_destruct(&eventGroup);
    return 0;
}

static int32_t mesh_command_main(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }

    const char* command = argv[1];
    if (strcmp(command, "status") == 0) {
        return command_status();
    } else if (strcmp(command, "on") == 0 || strcmp(command, "off") == 0) {
        return command_enable(strcmp(command, "on") == 0);
    } else if (strcmp(command, "backend") == 0) {
        return command_backend(argc, argv);
    } else if (strcmp(command, "region") == 0) {
        return command_region(argc, argv);
    } else if (strcmp(command, "preset") == 0) {
        return command_preset(argc, argv);
    } else if (strcmp(command, "channels") == 0) {
        return command_channels();
    } else if (strcmp(command, "join") == 0) {
        return command_join(argc, argv);
    } else if (strcmp(command, "inspect") == 0) {
        return command_inspect(argc, argv);
    }

    printf("mesh: unknown command: %s\n", command);
    print_usage();
    return 1;
}

extern "C" {

const AppManifest mesh_command_manifest = {
    .id = "mesh",
    .name = "mesh",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(mesh_command_main) },
    .flags = APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 6144, .desired_memory_capability = 0 },
};

}
