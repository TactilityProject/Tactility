// SPDX-License-Identifier: Apache-2.0
#include <development/server.h>

#include <development/credentials.h>
#include <development/protocol.h>
#include <development/session.h>
#include <development/trust.h>

#include <app/manager.h>
#include <app/start.h>
#include <service/manifest.h>
#include <service/paths.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/filesystem/fs.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <tactility/Memory.hpp>

#include <mbedtls/ssl.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

constexpr auto* TAG = "DevelopmentServer";

constexpr auto* SETTINGS_FILE = "server.properties";
constexpr auto* KEY_ENABLE_ON_BOOT = "enableOnBoot";

constexpr int LISTEN_BACKLOG = 2;
constexpr uint32_t ACCEPT_POLL_TIMEOUT_MS = 200;
constexpr uint32_t SOCKET_POLL_TIMEOUT_MS = 200;
constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 10000;
constexpr size_t LISTENER_STACK_SIZE = 8192;

constexpr MemoryPolicy MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

struct SessionSlot {
    bool active;
    bool shell;
    /** Set until the session app takes the session */
    DevelopmentSession* pending;
    AppInstanceId appId;
};

struct ServerData {
    char directory[FILE_MAX_PATH_STRING_LENGTH] = {};
    DevelopmentCredentials credentials {};
    bool credentialsLoaded = false;
    mbedtls_ssl_config config {};
    Thread* listener = nullptr;
    int listenFd = -1;
    uint16_t boundPort = 0;
    std::atomic<bool> stopRequested = false;
    // Guarded by stateMutex
    char fingerprint[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1] = {};
    bool listening = false;
    SessionSlot slots[DEVELOPMENT_SERVER_MAX_SESSIONS] = {};
};

struct GlobalMutex {
    Mutex handle {};
    GlobalMutex() { mutex_construct(&handle); }
    ~GlobalMutex() { mutex_destruct(&handle); }
};

// Serializes starting and stopping. Held while waiting for the listener and session app to stop,
// so those must never take it.
GlobalMutex controlMutex;
// Guards the server pointer and the fields of ServerData that the listener and session app share.
GlobalMutex stateMutex;

ServerData* server = nullptr;
uint16_t configuredPort = DEVELOPMENT_SERVER_DEFAULT_PORT;

void lockState() { mutex_lock(&stateMutex.handle); }
void unlockState() { mutex_unlock(&stateMutex.handle); }

// region Settings

bool getSettingsPath(const ServerData* data, char* outPath, size_t outSize) {
    const int written = snprintf(outPath, outSize, "%s/%s", data->directory, SETTINGS_FILE);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

bool loadEnableOnBoot(const ServerData* data) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getSettingsPath(data, path, sizeof(path))) {
        return false;
    }
    if (!path_exists(path)) {
        return false;
    }
    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        return false;
    }
    char value[8] = {};
    const bool enabled = properties_file_get(file, KEY_ENABLE_ON_BOOT, value, sizeof(value)) == ERROR_NONE && strcmp(value, "true") == 0;
    properties_file_close(file);
    return enabled;
}

void saveEnableOnBoot(const ServerData* data, bool enabled) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    const error_t directoryError = directory_make(data->directory, true);
    if ((directoryError != ERROR_NONE && directoryError != ERROR_ALREADY_EXISTS) || !getSettingsPath(data, path, sizeof(path))) {
        LOG_E(TAG, "Failed to save settings");
        return;
    }
    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s", path);
        return;
    }
    properties_file_set(file, KEY_ENABLE_ON_BOOT, enabled ? "true" : "false");
    if (properties_file_close(file) != ERROR_NONE) {
        LOG_E(TAG, "Failed to save %s", path);
    }
}

// endregion

// region Connections

int verifyClientCertificate(void* context, mbedtls_x509_crt* certificate, int depth, uint32_t* flags) {
    // Clients use self-signed certificates: the user decides whether to trust them
    if (depth == 0) {
        auto* session = static_cast<DevelopmentSession*>(context);
        if (development_fingerprint(certificate->raw.p, certificate->raw.len, session->fingerprint) != ERROR_NONE) {
            session->fingerprint[0] = '\0';
        }
    }
    *flags = 0;
    return 0;
}

bool isExpired(TickType_t start, uint32_t timeoutMs) {
    return xTaskGetTickCount() - start > pdMS_TO_TICKS(timeoutMs);
}

bool handshake(ServerData* data, DevelopmentSession* session) {
    const TickType_t start = xTaskGetTickCount();
    int result;
    while ((result = mbedtls_ssl_handshake(&session->ssl)) != 0) {
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LOG_W(TAG, "Handshake with %s failed: -0x%04x", session->peer_address, static_cast<unsigned>(-result));
            return false;
        }
        if (data->stopRequested || isExpired(start, HANDSHAKE_TIMEOUT_MS)) {
            LOG_W(TAG, "Handshake with %s timed out", session->peer_address);
            return false;
        }
    }
    return true;
}

// Reads byte by byte, so nothing after the newline is consumed
bool readLine(ServerData* data, DevelopmentSession* session, char* outLine, size_t outSize, size_t* outLength) {
    const TickType_t start = xTaskGetTickCount();
    size_t length = 0;
    while (true) {
        unsigned char character;
        const int result = mbedtls_ssl_read(&session->ssl, &character, 1);
        if (result == 1) {
            if (character == '\n') {
                outLine[length] = '\0';
                *outLength = length;
                return true;
            }
            if (length + 1 >= outSize) {
                return false;
            }
            outLine[length++] = static_cast<char>(character);
        } else if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return false;
        } else if (data->stopRequested || isExpired(start, HANDSHAKE_TIMEOUT_MS)) {
            return false;
        }
    }
}

bool readHello(ServerData* data, DevelopmentSession* session) {
    auto* line = static_cast<char*>(memory_alloc_with_policy(DEVELOPMENT_PROTOCOL_LINE_MAX, &MEMORY_POLICY));
    if (line == nullptr) {
        return false;
    }
    size_t length = 0;
    bool valid = false;
    if (readLine(data, session, line, DEVELOPMENT_PROTOCOL_LINE_MAX, &length)) {
        const char* message = nullptr;
        if (development_protocol_parse_hello(line, length, &session->hello, &message) == DevelopmentHelloError::None) {
            valid = true;
        } else {
            LOG_W(TAG, "Invalid hello from %s: %s", session->peer_address, message);
            development_session_send_status(session, "error", message);
        }
    }
    memory_free(line);
    return valid;
}

/** @return the reason why the session can't start now, or nullptr after reserving a slot for it */
const char* reserveSlot(ServerData* data, DevelopmentSession* session) {
    const bool shell = strcmp(session->hello.mode, "shell") == 0;
    lockState();
    int freeSlot = -1;
    bool shellBusy = false;
    for (int i = 0; i < DEVELOPMENT_SERVER_MAX_SESSIONS; i++) {
        const SessionSlot& slot = data->slots[i];
        if (!slot.active && freeSlot < 0) {
            freeSlot = i;
        }
        shellBusy = shellBusy || (shell && slot.active && slot.shell);
    }
    const char* error = shellBusy ? "shell busy" : (freeSlot < 0 ? "busy" : nullptr);
    if (error == nullptr) {
        data->slots[freeSlot] = { .active = true, .shell = shell, .pending = session, .appId = 0 };
        session->slot = freeSlot;
    }
    unlockState();
    return error;
}

void releaseSlot(ServerData* data, int slot) {
    lockState();
    data->slots[slot] = {};
    unlockState();
}

bool startSessionApp(ServerData* data, int slot) {
    char slotArgument[4];
    snprintf(slotArgument, sizeof(slotArgument), "%d", slot);
    const char* arguments[] = { slotArgument };
    AppStartContext context = app_start_context_for_manifest(&development_session_app_manifest);
    app_start_context_set_arguments_ext(&context, 1, arguments);
    AppInstanceId appId = 0;
    if (app_start_with_context(&context, &appId) != ERROR_NONE) {
        return false;
    }
    lockState();
    // Only this thread reserves slots, so an inactive slot means that the app already finished its session
    if (data->slots[slot].active) {
        data->slots[slot].appId = appId;
    }
    unlockState();
    return true;
}

void handleConnection(ServerData* data, int socket, const sockaddr_in& address) {
    const timeval timeout { .tv_sec = 0, .tv_usec = SOCKET_POLL_TIMEOUT_MS * 1000 };
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    DevelopmentSession* session = development_session_alloc(socket, &data->config);
    if (session == nullptr) {
        LOG_E(TAG, "Out of memory");
        close(socket);
        return;
    }
    inet_ntop(AF_INET, &address.sin_addr, session->peer_address, sizeof(session->peer_address));
    mbedtls_ssl_set_verify(&session->ssl, verifyClientCertificate, session);

    if (!handshake(data, session) || !readHello(data, session)) {
        development_session_free(session);
        return;
    }

    // The device information isn't sensitive, so it's handed out without a client certificate or trust
    if (strcmp(session->hello.mode, "info") == 0) {
        development_session_send_info(session, CONFIG_TT_DEVICE_VENDOR, CONFIG_TT_DEVICE_NAME_SIMPLE, CONFIG_TT_DEVICE_ID, TT_VERSION);
        development_session_free(session);
        return;
    }

    if (session->fingerprint[0] == '\0') {
        LOG_W(TAG, "Client %s has no certificate", session->peer_address);
        development_session_send_status(session, "error", "client certificate required");
        development_session_free(session);
        return;
    }

    session->trusted = development_trust_contains(data->directory, session->fingerprint);

    const char* busy = reserveSlot(data, session);
    if (busy != nullptr) {
        development_session_send_status(session, "error", busy);
        development_session_free(session);
        return;
    }

    const int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);

    LOG_I(TAG, "Connection from %s (%s)", session->peer_address, session->hello.client);
    if (!startSessionApp(data, session->slot)) {
        LOG_E(TAG, "Failed to start session");
        releaseSlot(data, session->slot);
        development_session_send_status(session, "error", "internal error");
        development_session_free(session);
    }
}

int32_t listenerMain(void* context) {
    auto* data = static_cast<ServerData*>(context);
    while (!data->stopRequested) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(data->listenFd, &readFds);
        timeval timeout { .tv_sec = 0, .tv_usec = ACCEPT_POLL_TIMEOUT_MS * 1000 };
        if (select(data->listenFd + 1, &readFds, nullptr, nullptr, &timeout) <= 0) {
            continue;
        }
        sockaddr_in address {};
        socklen_t addressLength = sizeof(address);
        const int socket = accept(data->listenFd, reinterpret_cast<sockaddr*>(&address), &addressLength);
        if (socket >= 0) {
            handleConnection(data, socket, address);
        }
    }
    return 0;
}

// endregion

// region Lifecycle

error_t loadCredentials(ServerData* data) {
    if (data->credentialsLoaded) {
        return ERROR_NONE;
    }
    const error_t error = development_credentials_load(data->directory, &data->credentials);
    if (error != ERROR_NONE) {
        development_credentials_free(&data->credentials);
        return error;
    }

    mbedtls_ssl_config_init(&data->config);
    int result = mbedtls_ssl_config_defaults(&data->config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result == 0) {
        mbedtls_ssl_conf_authmode(&data->config, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_min_tls_version(&data->config, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&data->config, MBEDTLS_SSL_VERSION_TLS1_2);
        result = mbedtls_ssl_conf_own_cert(&data->config, &data->credentials.certificate, &data->credentials.key);
    }
    if (result != 0) {
        LOG_E(TAG, "Failed to configure TLS: -0x%04x", static_cast<unsigned>(-result));
        mbedtls_ssl_config_free(&data->config);
        development_credentials_free(&data->credentials);
        return ERROR_RESOURCE;
    }
    data->credentialsLoaded = true;
    lockState();
    memcpy(data->fingerprint, data->credentials.fingerprint, sizeof(data->fingerprint));
    unlockState();
    LOG_I(TAG, "Device fingerprint %s", data->credentials.fingerprint);
    return ERROR_NONE;
}

error_t startListening(ServerData* data) {
    if (data->listener != nullptr) {
        return ERROR_NONE;
    }
    error_t error = loadCredentials(data);
    if (error != ERROR_NONE) {
        return error;
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return ERROR_RESOURCE;
    }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(configuredPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, LISTEN_BACKLOG) != 0) {
        LOG_E(TAG, "Failed to listen on port %u", static_cast<unsigned>(configuredPort));
        close(fd);
        return ERROR_RESOURCE;
    }
    socklen_t addressLength = sizeof(address);
    getsockname(fd, reinterpret_cast<sockaddr*>(&address), &addressLength);

    data->listenFd = fd;
    data->boundPort = ntohs(address.sin_port);
    data->stopRequested = false;
    data->listener = thread_alloc_full("development-server", LISTENER_STACK_SIZE, listenerMain, data, -1);
    if (data->listener != nullptr) {
        // Socket calls block outside the scheduler on POSIX, so a higher priority would starve other tasks there
        thread_set_priority(data->listener, THREAD_PRIORITY_IDLE);
    }
    if (data->listener == nullptr || thread_start(data->listener) != ERROR_NONE) {
        if (data->listener != nullptr) {
            thread_free(data->listener);
            data->listener = nullptr;
        }
        close(fd);
        data->listenFd = -1;
        return ERROR_RESOURCE;
    }

    lockState();
    data->listening = true;
    unlockState();
    LOG_I(TAG, "Listening on port %u", static_cast<unsigned>(data->boundPort));
    return ERROR_NONE;
}

void stopListening(ServerData* data) {
    if (data->listener != nullptr) {
        data->stopRequested = true;
        thread_join(data->listener, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(data->listener);
        data->listener = nullptr;
        close(data->listenFd);
        data->listenFd = -1;
    }

    AppInstanceId appIds[DEVELOPMENT_SERVER_MAX_SESSIONS];
    lockState();
    data->listening = false;
    for (int i = 0; i < DEVELOPMENT_SERVER_MAX_SESSIONS; i++) {
        appIds[i] = data->slots[i].appId;
    }
    unlockState();

    for (const AppInstanceId appId : appIds) {
        if (appId != 0 && app_manager_get_state(appId) != APP_INSTANCE_STATE_STOPPED) {
            app_manager_stop(appId);
        }
    }

    // Sessions that no app took yet
    for (int i = 0; i < DEVELOPMENT_SERVER_MAX_SESSIONS; i++) {
        lockState();
        DevelopmentSession* pending = data->slots[i].pending;
        if (pending != nullptr) {
            data->slots[i] = {};
        }
        unlockState();
        if (pending != nullptr) {
            development_session_free(pending);
        }
    }
}

void* createService(const ServiceManifest* /*manifest*/) {
    return tt::newObject<ServerData>();
}

void destroyService(const ServiceManifest* /*manifest*/, void* data) {
    tt::deleteObject(static_cast<ServerData*>(data));
}

error_t onStart(ServiceInstance* /*instance*/, void* context) {
    auto* data = static_cast<ServerData*>(context);
    if (data == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    const error_t error = service_paths_get_user_data_directory(DEVELOPMENT_SERVER_SERVICE_ID, data->directory, sizeof(data->directory));
    if (error != ERROR_NONE) {
        return error;
    }

    mutex_lock(&controlMutex.handle);
    lockState();
    server = data;
    unlockState();
    if (loadEnableOnBoot(data) && startListening(data) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start");
    }
    mutex_unlock(&controlMutex.handle);
    return ERROR_NONE;
}

void onStop(ServiceInstance* /*instance*/, void* context) {
    auto* data = static_cast<ServerData*>(context);
    mutex_lock(&controlMutex.handle);
    stopListening(data);
    lockState();
    server = nullptr;
    unlockState();
    lockState();
    data->fingerprint[0] = '\0';
    unlockState();
    if (data->credentialsLoaded) {
        mbedtls_ssl_config_free(&data->config);
        development_credentials_free(&data->credentials);
        data->credentialsLoaded = false;
    }
    mutex_unlock(&controlMutex.handle);
}

// endregion

} // namespace

extern "C" const ServiceManifest development_server_service_manifest = {
    .id = DEVELOPMENT_SERVER_SERVICE_ID,
    .create_service = createService,
    .destroy_service = destroyService,
    .on_start = onStart,
    .on_stop = onStop,
};

extern "C" {

error_t development_server_set_enabled(bool enabled) {
    mutex_lock(&controlMutex.handle);
    lockState();
    ServerData* data = server;
    unlockState();
    if (data == nullptr) {
        mutex_unlock(&controlMutex.handle);
        return ERROR_INVALID_STATE;
    }
    error_t error = ERROR_NONE;
    if (enabled) {
        error = startListening(data);
    } else {
        stopListening(data);
    }
    saveEnableOnBoot(data, enabled);
    mutex_unlock(&controlMutex.handle);
    return error;
}

bool development_server_is_enabled() {
    lockState();
    const bool listening = server != nullptr && server->listening;
    unlockState();
    return listening;
}

error_t development_server_get_fingerprint(char* out_fingerprint, size_t out_size) {
    if (out_size < DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1) {
        return ERROR_BUFFER_OVERFLOW;
    }
    // Avoids waiting for the control mutex once the credentials are loaded
    lockState();
    const bool loaded = server != nullptr && server->fingerprint[0] != '\0';
    if (loaded) {
        memcpy(out_fingerprint, server->fingerprint, sizeof(server->fingerprint));
    }
    unlockState();
    if (loaded) {
        return ERROR_NONE;
    }

    mutex_lock(&controlMutex.handle);
    lockState();
    ServerData* data = server;
    unlockState();
    error_t error = data != nullptr ? loadCredentials(data) : ERROR_INVALID_STATE;
    if (error == ERROR_NONE) {
        memcpy(out_fingerprint, data->credentials.fingerprint, DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1);
    }
    mutex_unlock(&controlMutex.handle);
    return error;
}

unsigned short development_server_get_port() {
    lockState();
    const uint16_t port = (server != nullptr && server->listening) ? server->boundPort : configuredPort;
    unlockState();
    return port;
}

} // extern "C"

DevelopmentSession* development_server_take_session(int slot) {
    if (slot < 0 || slot >= DEVELOPMENT_SERVER_MAX_SESSIONS) {
        return nullptr;
    }
    lockState();
    DevelopmentSession* session = nullptr;
    if (server != nullptr) {
        session = server->slots[slot].pending;
        server->slots[slot].pending = nullptr;
    }
    unlockState();
    return session;
}

void development_server_finish_session(DevelopmentSession* session) {
    const int slot = session->slot;
    development_session_free(session);
    lockState();
    if (server != nullptr) {
        server->slots[slot] = {};
    }
    unlockState();
}

namespace {

bool getDirectory(char* outDirectory) {
    lockState();
    const bool running = server != nullptr;
    if (running) {
        strcpy(outDirectory, server->directory);
    }
    unlockState();
    return running;
}

} // namespace

error_t development_server_trust(const char* fingerprint, const char* name) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    return getDirectory(directory) ? development_trust_add(directory, fingerprint, name) : ERROR_INVALID_STATE;
}

extern "C" error_t development_server_for_each_trusted_client(DevelopmentTrustedClientVisitor visitor, void* context) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    return getDirectory(directory) ? development_trust_for_each(directory, visitor, context) : ERROR_INVALID_STATE;
}

extern "C" error_t development_server_remove_trusted_client(const char* fingerprint) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    return getDirectory(directory) ? development_trust_remove(directory, fingerprint) : ERROR_INVALID_STATE;
}

void development_server_set_port(uint16_t port) {
    lockState();
    configuredPort = port;
    unlockState();
}
