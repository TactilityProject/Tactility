// SPDX-License-Identifier: Apache-2.0
#include <development/session.h>
#include <development/remote.h>
#include <development/requests.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <unistd.h>

#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

// A task with its stack in PSRAM can't write to internal flash, unless flash operations keep the cache enabled
#ifdef CONFIG_SPI_FLASH_AUTO_SUSPEND
constexpr uint16_t STACK_MEMORY_CAPABILITY = MEMORY_CAPABILITY_EXTERNAL;
#else
constexpr uint16_t STACK_MEMORY_CAPABILITY = 0;
#endif

namespace {

constexpr auto* TAG = "DevelopmentSession";

constexpr uint32_t PROMPT_TIMEOUT_MS = 60000;
constexpr uint32_t PROMPT_POLL_INTERVAL_MS = 200;
// Bounds the wait for socket input, which doesn't signal the event group
constexpr uint32_t PUMP_INTERVAL_MS = 20;
// How long the shell gets to exit by itself after its stdin closed
constexpr uint32_t SHELL_EXIT_TIMEOUT_MS = 2000;

constexpr size_t STDIN_BUFFER_SIZE = 256;
constexpr size_t STDOUT_BUFFER_SIZE = 1024;
constexpr size_t RECEIVE_BUFFER_SIZE = DEVELOPMENT_FRAME_HEADER_SIZE + DEVELOPMENT_FRAME_PAYLOAD_MAX;

constexpr MemoryPolicy MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

struct Buffers {
    uint8_t stdinBuffer[STDIN_BUFFER_SIZE];
    uint8_t stdoutBuffer[STDOUT_BUFFER_SIZE];
    uint8_t receive[RECEIVE_BUFFER_SIZE];
    uint8_t drain[DEVELOPMENT_FRAME_PAYLOAD_MAX];
};

struct Events {
    TaskEventGroup group {};
    AppEventSubscription subscription {};
    bool closeRequested = false;
};

bool isExpired(TickType_t start, uint32_t timeoutMs) {
    return xTaskGetTickCount() - start > pdMS_TO_TICKS(timeoutMs);
}

struct TrustJob {
    const char* fingerprint;
    const char* name;
    error_t result;
};

void trustWork(void* context) {
    auto* job = static_cast<TrustJob*>(context);
    job->result = development_server_trust(job->fingerprint, job->name);
}

/** Writes the trust file on the file operations task, because this task can have its stack in external RAM */
bool storeTrust(DevelopmentSession* session, Events& events) {
    FileOpsSubscription subscription {};
    if (file_ops_subscribe(&subscription, &events.group) != ERROR_NONE) {
        return false;
    }
    TrustJob job { session->fingerprint, session->hello.client, ERROR_TIMEOUT };
    bool stored = false;
    if (file_ops_queue(trustWork, &job, &subscription) == ERROR_NONE) {
        // The job lives on this stack, so the work must have finished before returning
        while (file_ops_await(&subscription, pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        }
        stored = job.result == ERROR_NONE;
    }
    file_ops_unsubscribe(&subscription);
    return stored;
}

DevelopmentPromptResult prompt(DevelopmentSession* session, AppInstanceId self, Events& events) {
    AppStartContext context;
    if (app_start_context_from_id(DEVELOPMENT_SERVER_PROMPT_APP_ID, &context) != ERROR_NONE) {
        LOG_W(TAG, "No prompt app found");
        return DEVELOPMENT_PROMPT_REJECT;
    }
    const char* arguments[] = { session->peer_address, session->hello.client, session->fingerprint, session->hello.mode };
    app_start_context_set_arguments_ext(&context, 4, arguments);
    app_start_context_set_parent(&context, self);
    AppInstanceId promptId = 0;
    if (app_start_with_context(&context, &promptId) != ERROR_NONE) {
        return DEVELOPMENT_PROMPT_REJECT;
    }

    DevelopmentPromptResult result = DEVELOPMENT_PROMPT_REJECT;
    const TickType_t start = xTaskGetTickCount();
    bool answered = false;
    while (!answered && !events.closeRequested && !isExpired(start, PROMPT_TIMEOUT_MS)) {
        task_event_group_wait_any(&events.group, nullptr, pdMS_TO_TICKS(PROMPT_POLL_INTERVAL_MS));
        AppEvent event {};
        while (app_event_poll(&events.subscription, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == promptId) {
                answered = true;
                if (event.result.result == DEVELOPMENT_PROMPT_ACCEPT_ONCE || event.result.result == DEVELOPMENT_PROMPT_ACCEPT_ALWAYS) {
                    result = static_cast<DevelopmentPromptResult>(event.result.result);
                }
            } else if (event.type == APP_EVENT_CLOSE) {
                events.closeRequested = true;
            }
        }
    }
    app_manager_stop(promptId);
    return result;
}

struct Bridge {
    DevelopmentSession* session;
    Buffers* buffers;
    AppStream* stdinStream;
    AppStream* stdoutStream;
    size_t receiveLength = 0;
    // Bytes of the first DATA frame in the receive buffer that the shell's stdin already accepted
    size_t dataOffset = 0;
    bool peerClosed = false;
};

// @return false when the frame couldn't be handled completely yet
bool handleFrame(Bridge& bridge, const DevelopmentFrame& frame) {
    switch (frame.type) {
        case DEVELOPMENT_FRAME_DATA: {
            const size_t remaining = frame.length - bridge.dataOffset;
            const size_t written = app_stream_write(bridge.stdinStream, frame.payload + bridge.dataOffset, remaining);
            if (written < remaining) {
                bridge.dataOffset += written;
                return false;
            }
            bridge.dataOffset = 0;
            return true;
        }
        case DEVELOPMENT_FRAME_RESIZE:
            if (frame.length == 4) {
                const auto columns = static_cast<uint16_t>((frame.payload[0] << 8) | frame.payload[1]);
                const auto rows = static_cast<uint16_t>((frame.payload[2] << 8) | frame.payload[3]);
                app_stream_set_window_size(bridge.stdoutStream, columns, rows);
            }
            return true;
        default:
            return true;
    }
}

void processFrames(Bridge& bridge) {
    size_t offset = 0;
    while (true) {
        DevelopmentFrame frame {};
        size_t consumed = 0;
        const auto result = development_frame_parse(bridge.buffers->receive + offset, bridge.receiveLength - offset, &frame, &consumed);
        if (result == DevelopmentFrameParseResult::Invalid) {
            LOG_W(TAG, "Invalid frame from %s", bridge.session->peer_address);
            bridge.peerClosed = true;
            break;
        }
        if (result == DevelopmentFrameParseResult::Incomplete || !handleFrame(bridge, frame)) {
            break;
        }
        offset += consumed;
    }
    if (offset > 0) {
        memmove(bridge.buffers->receive, bridge.buffers->receive + offset, bridge.receiveLength - offset);
        bridge.receiveLength -= offset;
    }
}

void receive(Bridge& bridge) {
    while (!bridge.peerClosed && bridge.receiveLength < RECEIVE_BUFFER_SIZE) {
        const int result = mbedtls_ssl_read(&bridge.session->ssl, bridge.buffers->receive + bridge.receiveLength, RECEIVE_BUFFER_SIZE - bridge.receiveLength);
        if (result > 0) {
            bridge.receiveLength += static_cast<size_t>(result);
            processFrames(bridge);
        } else if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        } else {
            bridge.peerClosed = true;
        }
    }
    // Retries a DATA frame that didn't fit in stdin before
    if (!bridge.peerClosed && bridge.receiveLength > 0) {
        processFrames(bridge);
    }
}

void sendOutput(Bridge& bridge) {
    size_t length;
    while ((length = app_stream_read(bridge.stdoutStream, bridge.buffers->drain, sizeof(bridge.buffers->drain))) > 0) {
        if (!bridge.peerClosed && !development_session_send_frame(bridge.session, DEVELOPMENT_FRAME_DATA, bridge.buffers->drain, static_cast<uint16_t>(length))) {
            bridge.peerClosed = true;
        }
    }
}

void runShell(DevelopmentSession* session, AppInstanceId self, Events& events) {
    auto* buffers = static_cast<Buffers*>(memory_alloc_with_policy(sizeof(Buffers), &MEMORY_POLICY));
    if (buffers == nullptr) {
        LOG_E(TAG, "Out of memory");
        return;
    }

    AppStream stdinStream {};
    AppStream stdoutStream {};
    const AppWindowSize windowSize { session->hello.columns, session->hello.rows };
    AppStreamBinding bindings[] = {
        { STDIN_FILENO, &stdinStream, buffers->stdinBuffer, sizeof(buffers->stdinBuffer), &events.group, {}, -1 },
        { STDOUT_FILENO, &stdoutStream, buffers->stdoutBuffer, sizeof(buffers->stdoutBuffer), &events.group, windowSize, STDERR_FILENO },
    };

    AppStartContext context;
    AppInstanceId shellId = 0;
    error_t error = app_start_context_from_id("shell", &context);
    if (error == ERROR_NONE) {
        // A command runs like sh -c instead of an interactive shell
        const char* commandArguments[] = { "shell", "-c", session->hello.command };
        if (session->hello.command[0] != '\0') {
            app_start_context_set_arguments_ext(&context, 3, commandArguments);
        }
        app_start_context_set_streams(&context, bindings, sizeof(bindings) / sizeof(bindings[0]));
        app_start_context_set_parent(&context, self);
        error = app_start_with_context(&context, &shellId);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start shell");
        memory_free(buffers);
        return;
    }

    Bridge bridge { session, buffers, &stdinStream, &stdoutStream };
    bool stdinClosed = false;
    bool shellDone = false;
    int32_t exitCode = 0;
    TickType_t stdinClosedTime = 0;

    while (!shellDone) {
        task_event_group_wait_any(&events.group, nullptr, pdMS_TO_TICKS(PUMP_INTERVAL_MS));

        receive(bridge);
        sendOutput(bridge);

        AppEvent event {};
        while (app_event_poll(&events.subscription, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == shellId) {
                shellDone = true;
                exitCode = event.result.result;
            } else if (event.type == APP_EVENT_CLOSE) {
                events.closeRequested = true;
            }
        }

        // Closing stdin unblocks a shell that waits for input, like a closed pipe does
        if ((bridge.peerClosed || events.closeRequested) && !stdinClosed) {
            app_stream_close(&stdinStream);
            stdinClosed = true;
            stdinClosedTime = xTaskGetTickCount();
        }
        if (stdinClosed && !shellDone && isExpired(stdinClosedTime, SHELL_EXIT_TIMEOUT_MS)) {
            break;
        }
    }

    // The shell may have written its last output right before exiting
    sendOutput(bridge);
    if (shellDone && !bridge.peerClosed) {
        const uint8_t code[4] = {
            static_cast<uint8_t>(static_cast<uint32_t>(exitCode) >> 24),
            static_cast<uint8_t>(static_cast<uint32_t>(exitCode) >> 16),
            static_cast<uint8_t>(static_cast<uint32_t>(exitCode) >> 8),
            static_cast<uint8_t>(static_cast<uint32_t>(exitCode)),
        };
        development_session_send_frame(session, DEVELOPMENT_FRAME_EXIT, code, sizeof(code));
    }

    // Unsubscribing guarantees that the shell no longer uses the streams before they go out of scope
    app_stream_unsubscribe(&stdinStream);
    app_stream_unsubscribe(&stdoutStream);
    app_manager_stop(shellId);
    memory_free(buffers);
}

// argv[0] is the server slot of the session
int32_t main(int argc, char* argv[]) {
    DevelopmentSession* session = argc == 1 ? development_server_take_session(atoi(argv[0])) : nullptr;
    if (session == nullptr) {
        return 2;
    }

    Events events;
    task_event_group_construct(&events.group);
    if (app_event_subscribe(&events.subscription, &events.group) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start session");
        development_session_send_status(session, "error", "internal error");
        task_event_group_destruct(&events.group);
        development_server_finish_session(session);
        return 2;
    }

    const AppInstanceId self = app_scheduler_current_app_id();
    bool accepted = session->trusted;
    if (!accepted) {
        const DevelopmentPromptResult result = prompt(session, self, events);
        accepted = result != DEVELOPMENT_PROMPT_REJECT;
        if (result == DEVELOPMENT_PROMPT_ACCEPT_ALWAYS && !storeTrust(session, events)) {
            LOG_E(TAG, "Failed to store trusted client");
        }
    }

    if (!accepted) {
        LOG_I(TAG, "Rejected %s", session->peer_address);
        development_session_send_status(session, "rejected", nullptr);
    } else if (development_session_send_status(session, "accepted", nullptr)) {
        LOG_I(TAG, "Accepted %s for %s", session->peer_address, session->hello.mode);
        if (strcmp(session->hello.mode, "sync") == 0) {
            development_requests_run(session, &events.group, &events.subscription, &events.closeRequested, DevelopmentRequestMode::Sync);
        } else if (strcmp(session->hello.mode, "app") == 0) {
            development_requests_run(session, &events.group, &events.subscription, &events.closeRequested, DevelopmentRequestMode::App);
        } else if (strcmp(session->hello.mode, "remote") == 0) {
            development_remote_run(session, &events.group, &events.subscription, &events.closeRequested);
        } else {
            runShell(session, self, events);
        }
    }

    app_event_unsubscribe(&events.subscription);
    task_event_group_destruct(&events.group);
    development_server_finish_session(session);
    LOG_I(TAG, "Session ended");
    return 0;
}

} // namespace

// The server starts the manifest that fits the mode of the connection
extern "C" const AppManifest development_remote_session_app_manifest = {
    .id = DEVELOPMENT_REMOTE_SESSION_APP_ID,
    .name = "Development Remote Session",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 8192, .desired_memory_capability = MEMORY_CAPABILITY_EXTERNAL },
};

extern "C" const AppManifest development_session_app_manifest = {
    .id = DEVELOPMENT_SESSION_APP_ID,
    .name = "Development Session",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 6144, .desired_memory_capability = STACK_MEMORY_CAPABILITY },
};
