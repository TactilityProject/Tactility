#include "doctest.h"

#include <development/credentials.h>
#include <development/protocol.h>
#include <development/server.h>
#include <development/session.h>

#include <app/event.h>
#include <app/io.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/package_manifest.h>

#include <service/manager.h>
#include <service/paths.h>

#include <tactility/delay.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/freertos/task.h>
#include <tactility/paths.h>

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

extern ServiceManifest app_internal_loader_service_manifest;

namespace {

const char* TEST_ROOT = "/tmp/tactility_development_test";
constexpr uint32_t TIMEOUT_MS = 10000;

// region Fixture

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

std::atomic<int> promptResult { DEVELOPMENT_PROMPT_REJECT };
std::atomic<int> promptCount { 0 };
char promptMode[16] = {};
char promptFingerprint[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1] = {};

int32_t promptMain(int argc, char* argv[]) {
    promptCount++;
    if (argc == 4) {
        strncpy(promptMode, argv[3], sizeof(promptMode) - 1);
        strncpy(promptFingerprint, argv[2], sizeof(promptFingerprint) - 1);
    }
    return promptResult;
}

const AppManifest promptManifest = {
    .id = DEVELOPMENT_SERVER_PROMPT_APP_ID,
    .name = "Prompt",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(promptMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

void writeOut(const std::string& text) {
    app_io_write(STDOUT_FILENO, text.data(), text.size());
}

// Echoes lines. "exit N" exits with code N and "size" prints the window size. "-c <command>" prints the command and exits with 7.
int32_t shellMain(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "-c") {
        writeOut("command:" + std::string(argv[2]) + "\n");
        return 7;
    }
    std::string line;
    char character;
    while (app_io_read(STDIN_FILENO, &character, 1) == 1) {
        if (character != '\n') {
            line += character;
            continue;
        }
        if (line.starts_with("exit ")) {
            return atoi(line.c_str() + 5);
        }
        if (line == "size") {
            AppWindowSize size {};
            app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &size);
            writeOut("size " + std::to_string(size.columns) + "x" + std::to_string(size.rows) + "\n");
        } else {
            writeOut("echo:" + line + "\n");
        }
        line.clear();
    }
    return 0;
}

const AppManifest shellManifest = {
    .id = "shell",
    .name = "Shell",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(shellMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

void ensureFixture() {
    static bool created = false;
    if (created) {
        return;
    }
    created = true;

    std::filesystem::remove_all(TEST_ROOT);
    std::filesystem::create_directories(TEST_ROOT);
    REQUIRE_NE(file_system_add("data", &fakeDataApi, const_cast<char*>(TEST_ROOT)), nullptr);
    if (service_manager_find_instance(APP_LOADER_MEMORY_SERVICE_ID) == nullptr) {
        REQUIRE_EQ(service_manager_add(&app_internal_loader_service_manifest, true), ERROR_NONE);
    }
    REQUIRE_EQ(app_manager_add(&promptManifest), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&shellManifest), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&development_session_app_manifest), ERROR_NONE);

    development_server_set_port(0);
    REQUIRE_EQ(service_manager_add(&development_server_service_manifest, true), ERROR_NONE);
    REQUIRE_EQ(development_server_set_enabled(true), ERROR_NONE);
}

/** A client certificate, stored in its own directory like the device certificate */
struct Identity {
    DevelopmentCredentials credentials {};

    explicit Identity(const char* name) {
        const std::string directory = std::string(TEST_ROOT) + "/clients/" + name;
        REQUIRE_EQ(development_credentials_load(directory.c_str(), &credentials), ERROR_NONE);
    }

    ~Identity() { development_credentials_free(&credentials); }
};

Identity& identity(int index) {
    static Identity first("first");
    static Identity second("second");
    static Identity third("third");
    static Identity fourth("fourth");
    static Identity fifth("fifth");
    switch (index) {
        case 0: return first;
        case 1: return second;
        case 2: return third;
        case 3: return fourth;
        default: return fifth;
    }
}

bool isExpired(TickType_t start, uint32_t timeoutMs) {
    return xTaskGetTickCount() - start > pdMS_TO_TICKS(timeoutMs);
}

// endregion

// region Client

int bioSend(void* context, const unsigned char* data, size_t length) {
    const ssize_t sent = send(*static_cast<int*>(context), data, length, MSG_NOSIGNAL);
    if (sent >= 0) {
        return static_cast<int>(sent);
    }
    return (errno == EAGAIN || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}

int bioReceive(void* context, unsigned char* data, size_t length) {
    const ssize_t received = recv(*static_cast<int*>(context), data, length, 0);
    if (received > 0) {
        return static_cast<int>(received);
    }
    if (received == 0) {
        return MBEDTLS_ERR_NET_CONN_RESET;
    }
    return (errno == EAGAIN || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

class TestClient {

    int socketFd = -1;
    mbedtls_ssl_config config {};
    mbedtls_ssl_context ssl {};
    std::string received;
    bool closed = false;

    // @return false on timeout or when the connection closed
    bool receiveMore(TickType_t start, uint32_t timeoutMs) {
        unsigned char buffer[512];
        while (!isExpired(start, timeoutMs)) {
            const int result = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
            if (result > 0) {
                received.append(reinterpret_cast<char*>(buffer), result);
                return true;
            }
            if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
                closed = true;
                return false;
            }
        }
        return false;
    }

public:

    explicit TestClient(Identity* identity) {
        mbedtls_ssl_config_init(&config);
        mbedtls_ssl_init(&ssl);
        REQUIRE_EQ(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT), 0);
        mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
        if (identity != nullptr) {
            REQUIRE_EQ(mbedtls_ssl_conf_own_cert(&config, &identity->credentials.certificate, &identity->credentials.key), 0);
        }
        REQUIRE_EQ(mbedtls_ssl_setup(&ssl, &config), 0);
    }

    ~TestClient() {
        if (socketFd >= 0) {
            mbedtls_ssl_close_notify(&ssl);
            close(socketFd);
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
    }

    bool connect() {
        socketFd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(socketFd >= 0);
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(development_server_get_port());
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        int result;
        do {
            result = ::connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        } while (result != 0 && errno == EINTR);
        REQUIRE_EQ(result, 0);
        timeval timeout { .tv_sec = 0, .tv_usec = 100000 };
        setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        mbedtls_ssl_set_bio(&ssl, &socketFd, bioSend, bioReceive, nullptr);

        const TickType_t start = xTaskGetTickCount();
        while ((result = mbedtls_ssl_handshake(&ssl)) != 0) {
            if ((result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) || isExpired(start, TIMEOUT_MS)) {
                return false;
            }
        }
        return true;
    }

    std::string serverFingerprint() {
        const mbedtls_x509_crt* certificate = mbedtls_ssl_get_peer_cert(&ssl);
        REQUIRE_NE(certificate, nullptr);
        char fingerprint[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1];
        REQUIRE_EQ(development_fingerprint(certificate->raw.p, certificate->raw.len, fingerprint), ERROR_NONE);
        return fingerprint;
    }

    void write(const std::string& data) {
        size_t offset = 0;
        const TickType_t start = xTaskGetTickCount();
        while (offset < data.size()) {
            const int result = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(data.data()) + offset, data.size() - offset);
            if (result > 0) {
                offset += static_cast<size_t>(result);
            } else {
                REQUIRE((result == MBEDTLS_ERR_SSL_WANT_WRITE || result == MBEDTLS_ERR_SSL_WANT_READ));
                REQUIRE_FALSE(isExpired(start, TIMEOUT_MS));
            }
        }
    }

    /** @param[in] extraFields JSON fields to add, starting with a comma */
    void sendHello(const std::string& mode = "shell", int protocol = DEVELOPMENT_PROTOCOL_VERSION, const std::string& extraFields = "") {
        write(R"({"protocol":)" + std::to_string(protocol) + R"(,"mode":")" + mode + R"(","client":"test","columns":100,"rows":30)" + extraFields + "}\n");
    }

    std::string readLine() {
        const TickType_t start = xTaskGetTickCount();
        size_t newline;
        while ((newline = received.find('\n')) == std::string::npos) {
            if (!receiveMore(start, TIMEOUT_MS)) {
                return {};
            }
        }
        std::string line = received.substr(0, newline);
        received.erase(0, newline + 1);
        return line;
    }

    void sendFrame(DevelopmentFrameType type, const std::string& payload) {
        uint8_t header[DEVELOPMENT_FRAME_HEADER_SIZE];
        development_frame_write_header(type, static_cast<uint16_t>(payload.size()), header);
        write(std::string(reinterpret_cast<char*>(header), sizeof(header)) + payload);
    }

    void sendResize(uint16_t columns, uint16_t rows) {
        const char payload[4] = {
            static_cast<char>(columns >> 8), static_cast<char>(columns & 0xFF),
            static_cast<char>(rows >> 8), static_cast<char>(rows & 0xFF)
        };
        sendFrame(DEVELOPMENT_FRAME_RESIZE, std::string(payload, sizeof(payload)));
    }

    /** @return false on timeout or when the connection closed */
    bool readFrame(DevelopmentFrameType& outType, std::string& outPayload) {
        const TickType_t start = xTaskGetTickCount();
        while (true) {
            DevelopmentFrame frame {};
            size_t consumed = 0;
            const auto result = development_frame_parse(reinterpret_cast<const uint8_t*>(received.data()), received.size(), &frame, &consumed);
            REQUIRE_NE(result, DevelopmentFrameParseResult::Invalid);
            if (result == DevelopmentFrameParseResult::Ok) {
                outType = frame.type;
                outPayload.assign(reinterpret_cast<const char*>(frame.payload), frame.length);
                received.erase(0, consumed);
                return true;
            }
            if (!receiveMore(start, TIMEOUT_MS)) {
                return false;
            }
        }
    }

    /** Reads DATA frames until the output contains @a text */
    bool readOutputUntil(const std::string& text) {
        std::string output;
        DevelopmentFrameType type;
        std::string payload;
        while (output.find(text) == std::string::npos) {
            if (!readFrame(type, payload) || type != DEVELOPMENT_FRAME_DATA) {
                return false;
            }
            output += payload;
        }
        return true;
    }

    /** @return true when the server closes the connection within the timeout */
    bool waitForClose() {
        const TickType_t start = xTaskGetTickCount();
        while (!closed && !isExpired(start, TIMEOUT_MS)) {
            receiveMore(start, TIMEOUT_MS);
        }
        return closed;
    }

    /** Closes the connection without a TLS close_notify */
    void drop() {
        close(socketFd);
        socketFd = -1;
    }
};

std::string readHostFile(const std::string& path);

/** Connects and sends a hello. @return the status line */
std::string connectAndGreet(TestClient& client, const std::string& mode = "shell", int protocol = DEVELOPMENT_PROTOCOL_VERSION, const std::string& extraFields = "") {
    REQUIRE(client.connect());
    client.sendHello(mode, protocol, extraFields);
    return client.readLine();
}

/** Connects and sends a hello, retrying while a previous session still ends, as that happens asynchronously */
std::unique_ptr<TestClient> greetWhenIdle(Identity* identity, std::string& outStatus, const std::string& mode = "shell", const std::string& extraFields = "") {
    const TickType_t start = xTaskGetTickCount();
    while (true) {
        auto client = std::make_unique<TestClient>(identity);
        outStatus = connectAndGreet(*client, mode, DEVELOPMENT_PROTOCOL_VERSION, extraFields);
        if (outStatus.find("busy") == std::string::npos) {
            return client;
        }
        REQUIRE_FALSE(isExpired(start, TIMEOUT_MS));
        delay_millis(50);
    }
}

constexpr auto* ACCEPTED = R"({"status":"accepted"})";
constexpr auto* REJECTED = R"({"status":"rejected"})";

// endregion

} // namespace

TEST_CASE("an info hello is answered without a prompt") {
    ensureFixture();
    promptCount = 0;

    TestClient client(&identity(0));
    const std::string line = connectAndGreet(client, "info");
    CHECK_NE(line.find(R"("status":"accepted")"), std::string::npos);
    CHECK_NE(line.find(std::string(R"("id":")") + CONFIG_TT_DEVICE_ID + "\""), std::string::npos);
    CHECK_EQ(promptCount, 0);
}

TEST_CASE("a client that is accepted once gets a shell") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;
    promptCount = 0;

    std::string status;
    auto client = greetWhenIdle(&identity(0), status);
    REQUIRE_EQ(status, ACCEPTED);
    char fingerprint[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1];
    REQUIRE_EQ(development_server_get_fingerprint(fingerprint, sizeof(fingerprint)), ERROR_NONE);
    CHECK_EQ(client->serverFingerprint(), std::string(fingerprint));
    CHECK_EQ(promptCount, 1);
    CHECK_EQ(std::string(promptMode), "shell");
    CHECK_EQ(std::string(promptFingerprint), std::string(identity(0).credentials.fingerprint));

    client->sendFrame(DEVELOPMENT_FRAME_DATA, "hello\n");
    CHECK(client->readOutputUntil("echo:hello\n"));

    client->sendFrame(DEVELOPMENT_FRAME_DATA, "size\n");
    CHECK(client->readOutputUntil("size 100x30\n"));
    client->sendResize(132, 43);
    client->sendFrame(DEVELOPMENT_FRAME_DATA, "size\n");
    CHECK(client->readOutputUntil("size 132x43\n"));

    client->sendFrame(DEVELOPMENT_FRAME_DATA, "exit 3\n");
    DevelopmentFrameType type;
    std::string payload;
    REQUIRE(client->readFrame(type, payload));
    CHECK_EQ(type, DEVELOPMENT_FRAME_EXIT);
    CHECK_EQ(payload, std::string("\0\0\0\3", 4));
    CHECK(client->waitForClose());
}

TEST_CASE("a client that is accepted once is asked for again") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;

    promptCount = 0;
    std::string firstStatus;
    auto first = greetWhenIdle(&identity(0), firstStatus);
    REQUIRE_EQ(firstStatus, ACCEPTED);
    first->sendFrame(DEVELOPMENT_FRAME_DATA, "exit 0\n");
    CHECK(first->waitForClose());

    std::string secondStatus;
    auto second = greetWhenIdle(&identity(0), secondStatus);
    REQUIRE_EQ(secondStatus, ACCEPTED);
    CHECK_EQ(promptCount, 2);
}

TEST_CASE("a rejected client is disconnected") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_REJECT;

    std::string clientStatus;
    auto client = greetWhenIdle(&identity(0), clientStatus);
    CHECK_EQ(clientStatus, REJECTED);
    CHECK(client->waitForClose());
}

TEST_CASE("a prompt result that is not a known answer rejects the client") {
    ensureFixture();
    promptResult = 7;

    std::string clientStatus;
    auto client = greetWhenIdle(&identity(0), clientStatus);
    CHECK_EQ(clientStatus, REJECTED);
}

TEST_CASE("a client that is always accepted is not asked for again, also after a restart") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ALWAYS;

    promptCount = 0;
    std::string firstStatus;
    auto first = greetWhenIdle(&identity(1), firstStatus);
    REQUIRE_EQ(firstStatus, ACCEPTED);
    CHECK_EQ(promptCount, 1);
    first->sendFrame(DEVELOPMENT_FRAME_DATA, "exit 0\n");
    CHECK(first->waitForClose());

    char fingerprintBefore[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1];
    REQUIRE_EQ(development_server_get_fingerprint(fingerprintBefore, sizeof(fingerprintBefore)), ERROR_NONE);
    REQUIRE_EQ(service_manager_stop(DEVELOPMENT_SERVER_SERVICE_ID), ERROR_NONE);
    REQUIRE_EQ(service_manager_start(DEVELOPMENT_SERVER_SERVICE_ID), ERROR_NONE);
    // The enabled state was persisted
    REQUIRE(development_server_is_enabled());
    char fingerprintAfter[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1];
    REQUIRE_EQ(development_server_get_fingerprint(fingerprintAfter, sizeof(fingerprintAfter)), ERROR_NONE);
    CHECK_EQ(std::string(fingerprintBefore), std::string(fingerprintAfter));

    promptResult = DEVELOPMENT_PROMPT_REJECT;
    std::string secondStatus;
    auto second = greetWhenIdle(&identity(1), secondStatus);
    REQUIRE_EQ(secondStatus, ACCEPTED);
    CHECK_EQ(promptCount, 1);
    second->sendFrame(DEVELOPMENT_FRAME_DATA, "hi\n");
    CHECK(second->readOutputUntil("echo:hi\n"));
}

TEST_CASE("a client without certificate is refused") {
    ensureFixture();
    TestClient anonymous(nullptr);
    const std::string status = connectAndGreet(anonymous);
    CHECK_NE(status.find("client certificate required"), std::string::npos);
    CHECK(anonymous.waitForClose());
}

TEST_CASE("an unsupported mode or protocol version is refused") {
    ensureFixture();
    promptCount = 0;

    TestClient ftp(&identity(0));
    CHECK_EQ(connectAndGreet(ftp, "ftp"), R"({"status":"error","message":"unsupported mode"})");

    TestClient future(&identity(0));
    CHECK_EQ(connectAndGreet(future, "shell", 2), R"({"status":"error","message":"unsupported protocol version"})");
    CHECK_EQ(promptCount, 0);
}

TEST_CASE("a shell hello with a command runs it with -c and reports its exit code") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;

    std::string status;
    auto client = greetWhenIdle(&identity(0), status, "shell", R"(,"command":"ls -la /data")");
    REQUIRE_EQ(status, ACCEPTED);
    CHECK(client->readOutputUntil("command:ls -la /data\n"));
    DevelopmentFrameType type;
    std::string payload;
    REQUIRE(client->readFrame(type, payload));
    CHECK_EQ(type, DEVELOPMENT_FRAME_EXIT);
    CHECK_EQ(payload, std::string("\0\0\0\7", 4));
    CHECK(client->waitForClose());
}

TEST_CASE("a second shell is refused while a shell is active, other modes are not") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;

    std::string firstStatus;
    auto first = greetWhenIdle(&identity(0), firstStatus);
    REQUIRE_EQ(firstStatus, ACCEPTED);

    TestClient other(&identity(0));
    CHECK_EQ(connectAndGreet(other), R"({"status":"error","message":"shell busy"})");

    TestClient sync(&identity(0));
    CHECK_EQ(connectAndGreet(sync, "sync"), ACCEPTED);

    first->sendFrame(DEVELOPMENT_FRAME_DATA, "still here\n");
    CHECK(first->readOutputUntil("echo:still here\n"));
}

TEST_CASE("at most 3 sessions run at once") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;

    std::string status;
    auto shell = greetWhenIdle(&identity(0), status);
    REQUIRE_EQ(status, ACCEPTED);
    auto sync = greetWhenIdle(&identity(0), status, "sync");
    REQUIRE_EQ(status, ACCEPTED);
    auto app = greetWhenIdle(&identity(0), status, "app");
    REQUIRE_EQ(status, ACCEPTED);

    TestClient fourth(&identity(0));
    CHECK_EQ(connectAndGreet(fourth, "sync"), R"({"status":"error","message":"busy"})");

    // All sessions keep working
    shell->sendFrame(DEVELOPMENT_FRAME_DATA, "one\n");
    CHECK(shell->readOutputUntil("echo:one\n"));
    std::string message;
    sync->sendFrame(DEVELOPMENT_FRAME_SYNC_STAT, "/");
    DevelopmentFrameType type;
    REQUIRE(sync->readFrame(type, message));
    CHECK_EQ(type, DEVELOPMENT_FRAME_SYNC_STAT_RESULT);
    app->sendFrame(DEVELOPMENT_FRAME_APP_UNINSTALL, "devtest.not.installed");
    REQUIRE(app->readFrame(type, message));
    CHECK_EQ(type, DEVELOPMENT_FRAME_OK);

    REQUIRE_EQ(development_server_set_enabled(false), ERROR_NONE);
    CHECK(shell->waitForClose());
    CHECK(sync->waitForClose());
    CHECK(app->waitForClose());
    REQUIRE_EQ(development_server_set_enabled(true), ERROR_NONE);
}

TEST_CASE("clients that are always accepted at the same time are all stored") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ALWAYS;

    // Both hellos are sent before either answer is read, so both sessions prompt and store concurrently.
    // The listener handles one handshake and hello at a time.
    TestClient first(&identity(3));
    TestClient second(&identity(4));
    REQUIRE(first.connect());
    first.sendHello("sync");
    REQUIRE(second.connect());
    second.sendHello("sync");
    REQUIRE_EQ(first.readLine(), ACCEPTED);
    REQUIRE_EQ(second.readLine(), ACCEPTED);

    char directory[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(service_paths_get_user_data_directory(DEVELOPMENT_SERVER_SERVICE_ID, directory, sizeof(directory)), ERROR_NONE);
    const std::string trusted = readHostFile(std::string(directory) + "/trusted_clients.txt");
    CHECK_NE(trusted.find(identity(3).credentials.fingerprint), std::string::npos);
    CHECK_NE(trusted.find(identity(4).credentials.fingerprint), std::string::npos);
}

TEST_CASE("a session ends when its client disconnects") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;

    {
        std::string firstStatus;
        auto first = greetWhenIdle(&identity(0), firstStatus);
        REQUIRE_EQ(firstStatus, ACCEPTED);
        first->sendFrame(DEVELOPMENT_FRAME_DATA, "partial line without newline");
        first->drop();
    }

    std::string secondStatus;
    auto second = greetWhenIdle(&identity(0), secondStatus);
    REQUIRE_EQ(secondStatus, ACCEPTED);
    second->sendFrame(DEVELOPMENT_FRAME_DATA, "again\n");
    CHECK(second->readOutputUntil("echo:again\n"));
}

TEST_CASE("disabling the server ends the active session") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;

    std::string clientStatus;
    auto client = greetWhenIdle(&identity(0), clientStatus);
    REQUIRE_EQ(clientStatus, ACCEPTED);

    REQUIRE_EQ(development_server_set_enabled(false), ERROR_NONE);
    CHECK_FALSE(development_server_is_enabled());
    CHECK(client->waitForClose());

    REQUIRE_EQ(development_server_set_enabled(true), ERROR_NONE);
    CHECK(development_server_is_enabled());
}

// region Sync

namespace {

std::string syncPath(const std::string& child) {
    return std::string(TEST_ROOT) + "/sync/" + child;
}

std::string readHostFile(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    REQUIRE_NE(file, nullptr);
    std::string content;
    char buffer[4096];
    size_t length;
    while ((length = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, length);
    }
    fclose(file);
    return content;
}

std::unique_ptr<TestClient> openSync() {
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;
    std::string status;
    auto client = greetWhenIdle(&identity(0), status, "sync");
    REQUIRE_EQ(status, ACCEPTED);
    return client;
}

/** @return the reply frame type, with its payload in @a outPayload */
DevelopmentFrameType push(TestClient& client, const std::string& path, const std::string& content, std::string& outPayload, DevelopmentFrameType request = DEVELOPMENT_FRAME_SYNC_PUSH) {
    client.sendFrame(request, path);
    for (size_t offset = 0; offset < content.size(); offset += DEVELOPMENT_FRAME_PAYLOAD_MAX) {
        client.sendFrame(DEVELOPMENT_FRAME_DATA, content.substr(offset, DEVELOPMENT_FRAME_PAYLOAD_MAX));
    }
    client.sendFrame(DEVELOPMENT_FRAME_DONE, "");
    DevelopmentFrameType type;
    REQUIRE(client.readFrame(type, outPayload));
    return type;
}

/** @return the frame type that ended the transfer: DONE, or FAIL with its message in @a outContent */
DevelopmentFrameType pull(TestClient& client, const std::string& path, std::string& outContent) {
    client.sendFrame(DEVELOPMENT_FRAME_SYNC_PULL, path);
    outContent.clear();
    DevelopmentFrameType type;
    std::string payload;
    while (true) {
        REQUIRE(client.readFrame(type, payload));
        if (type == DEVELOPMENT_FRAME_FAIL) {
            outContent = payload;
        }
        if (type != DEVELOPMENT_FRAME_DATA) {
            return type;
        }
        outContent += payload;
    }
}

/** @return the file type, with its size in @a outSize */
uint8_t stat(TestClient& client, const std::string& path, uint64_t& outSize) {
    client.sendFrame(DEVELOPMENT_FRAME_SYNC_STAT, path);
    DevelopmentFrameType type;
    std::string payload;
    REQUIRE(client.readFrame(type, payload));
    REQUIRE_EQ(type, DEVELOPMENT_FRAME_SYNC_STAT_RESULT);
    REQUIRE_EQ(payload.size(), 9);
    outSize = 0;
    for (int i = 1; i < 9; i++) {
        outSize = (outSize << 8) | static_cast<uint8_t>(payload[i]);
    }
    return static_cast<uint8_t>(payload[0]);
}

std::string makeContent(size_t size) {
    std::string content(size, '\0');
    for (size_t i = 0; i < size; i++) {
        content[i] = static_cast<char>((i * 31 + i / 7) & 0xFF);
    }
    return content;
}

} // namespace

TEST_CASE("sync pushes, stats and pulls a file") {
    ensureFixture();
    auto client = openSync();
    const std::string path = syncPath("created/parents/file.bin");
    const std::string content = makeContent(100 * 1024 + 17);

    std::string payload;
    REQUIRE_EQ(push(*client, path, content, payload), DEVELOPMENT_FRAME_OK);
    CHECK_EQ(readHostFile(path), content);

    uint64_t size = 0;
    CHECK_EQ(stat(*client, path, size), DEVELOPMENT_SYNC_FILE_REGULAR);
    CHECK_EQ(size, content.size());
    CHECK_EQ(stat(*client, syncPath("created"), size), DEVELOPMENT_SYNC_FILE_DIRECTORY);
    CHECK_EQ(stat(*client, syncPath("missing"), size), DEVELOPMENT_SYNC_FILE_MISSING);

    std::string pulled;
    REQUIRE_EQ(pull(*client, path, pulled), DEVELOPMENT_FRAME_DONE);
    CHECK_EQ(pulled, content);

    // Overwrites, and handles empty files
    REQUIRE_EQ(push(*client, path, "", payload), DEVELOPMENT_FRAME_OK);
    REQUIRE_EQ(pull(*client, path, pulled), DEVELOPMENT_FRAME_DONE);
    CHECK(pulled.empty());
}

TEST_CASE("sync reports failures and keeps the connection usable") {
    ensureFixture();
    auto client = openSync();
    std::string payload;
    std::string pulled;

    REQUIRE_EQ(pull(*client, syncPath("missing"), pulled), DEVELOPMENT_FRAME_FAIL);
    REQUIRE_EQ(pull(*client, "relative/path", pulled), DEVELOPMENT_FRAME_FAIL);
    CHECK_EQ(pulled, "invalid path");

    std::filesystem::create_directories(syncPath("directory"));
    REQUIRE_EQ(pull(*client, syncPath("directory"), pulled), DEVELOPMENT_FRAME_FAIL);
    CHECK_EQ(pulled, "is a directory");
    REQUIRE_EQ(push(*client, syncPath("directory"), "data", payload), DEVELOPMENT_FRAME_FAIL);
    CHECK_EQ(payload, "is a directory");

    REQUIRE_EQ(push(*client, syncPath("after-failures.txt"), "still works", payload), DEVELOPMENT_FRAME_OK);
    CHECK_EQ(readHostFile(syncPath("after-failures.txt")), "still works");
}

TEST_CASE("sync removes a partially pushed file when the client disconnects") {
    ensureFixture();
    const std::string path = syncPath("partial.bin");
    {
        auto client = openSync();
        // The file exists before the interrupted push, so its removal is observable
        std::string payload;
        REQUIRE_EQ(push(*client, path, makeContent(500), payload), DEVELOPMENT_FRAME_OK);
        client->sendFrame(DEVELOPMENT_FRAME_SYNC_PUSH, path);
        client->sendFrame(DEVELOPMENT_FRAME_DATA, makeContent(300));
        client->drop();
    }
    // The next session starts once the interrupted one ended
    auto client = openSync();
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("sync rejects an unexpected frame by closing the connection") {
    ensureFixture();
    auto client = openSync();
    client->sendFrame(DEVELOPMENT_FRAME_RESIZE, "1234");
    CHECK(client->waitForClose());
}

// endregion

// region App

namespace {

std::atomic<int> runCount { 0 };

int32_t runnableMain(int, char*[]) {
    runCount++;
    return 0;
}

const AppManifest runnableManifest = {
    .id = "devtest.runnable",
    .name = "Runnable",
    .category = APP_CATEGORY_USER,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(runnableMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

void ensureAppFixture() {
    ensureFixture();
    static bool added = false;
    if (!added) {
        added = true;
        REQUIRE_EQ(app_manager_add(&runnableManifest), ERROR_NONE);
    }
}

void addTarEntry(std::string& tar, const std::string& name, const std::string& content) {
    char header[512] = {};
    strncpy(header, name.c_str(), 99);
    snprintf(header + 100, 8, "%07o", 0644);
    snprintf(header + 108, 8, "%07o", 0);
    snprintf(header + 116, 8, "%07o", 0);
    snprintf(header + 124, 12, "%011o", static_cast<unsigned>(content.size()));
    snprintf(header + 136, 12, "%011o", 0);
    memset(header + 148, ' ', 8);
    header[156] = '0';
    memcpy(header + 257, "ustar", 6);
    memcpy(header + 263, "00", 2);
    unsigned checksum = 0;
    for (char character : header) {
        checksum += static_cast<unsigned char>(character);
    }
    snprintf(header + 148, 8, "%06o", checksum);
    header[155] = ' ';
    tar.append(header, sizeof(header));
    tar += content;
    tar.append((512 - content.size() % 512) % 512, '\0');
}

std::string makePackage() {
    std::string tar;
    addTarEntry(tar, "manifest.properties",
        "manifest.version=0.3\n"
        "id=devtest.package\n"
        "version.name=1.0.0\n"
        "version.code=1\n"
        "target.sdk=" TT_VERSION "\n"
        "app.0.id=devtest.package.app\n"
        "app.0.name=Packaged\n"
        "app.0.binary=app\n");
    addTarEntry(tar, "app", "not a real binary");
    tar.append(1024, '\0');
    return tar;
}

std::unique_ptr<TestClient> openApp() {
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ONCE;
    std::string status;
    auto client = greetWhenIdle(&identity(0), status, "app");
    REQUIRE_EQ(status, ACCEPTED);
    return client;
}

/** @return the answer type, with a FAIL message in @a outMessage */
DevelopmentFrameType request(TestClient& client, DevelopmentFrameType type, const std::string& payload, std::string& outMessage) {
    client.sendFrame(type, payload);
    DevelopmentFrameType answer;
    REQUIRE(client.readFrame(answer, outMessage));
    return answer;
}

std::string temporaryDirectory() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(paths_get_temp_path(path, sizeof(path)), ERROR_NONE);
    return path;
}

bool isDirectoryEmptyOrMissing(const std::string& path) {
    return !std::filesystem::exists(path) || std::filesystem::is_empty(path);
}

} // namespace

TEST_CASE("app mode runs an app and reports unknown ones") {
    ensureAppFixture();
    auto client = openApp();
    std::string message;
    const int before = runCount;
    REQUIRE_EQ(request(*client, DEVELOPMENT_FRAME_APP_RUN, runnableManifest.id, message), DEVELOPMENT_FRAME_OK);
    const TickType_t start = xTaskGetTickCount();
    while (runCount == before && !isExpired(start, TIMEOUT_MS)) {
        delay_millis(10);
    }
    CHECK_EQ(runCount, before + 1);

    REQUIRE_EQ(request(*client, DEVELOPMENT_FRAME_APP_RUN, "devtest.missing", message), DEVELOPMENT_FRAME_FAIL);
    CHECK_EQ(message, "not found");
}

TEST_CASE("app mode installs and uninstalls a package") {
    ensureAppFixture();
    auto client = openApp();
    std::string message;

    REQUIRE_EQ(push(*client, "devtest.app", makePackage(), message, DEVELOPMENT_FRAME_APP_INSTALL), DEVELOPMENT_FRAME_OK);
    AppManifest manifest;
    CHECK_EQ(app_manager_find_manifest("devtest.package.app", &manifest), ERROR_NONE);
    CHECK(isDirectoryEmptyOrMissing(temporaryDirectory()));

    REQUIRE_EQ(request(*client, DEVELOPMENT_FRAME_APP_UNINSTALL, "devtest.package", message), DEVELOPMENT_FRAME_OK);
    CHECK_NE(app_manager_find_manifest("devtest.package.app", &manifest), ERROR_NONE);
    // Uninstalling a package that isn't installed succeeds
    REQUIRE_EQ(request(*client, DEVELOPMENT_FRAME_APP_UNINSTALL, "devtest.package", message), DEVELOPMENT_FRAME_OK);
}

TEST_CASE("app mode refuses invalid packages and file names") {
    ensureAppFixture();
    auto client = openApp();
    std::string message;

    REQUIRE_EQ(push(*client, "garbage.app", makeContent(3000), message, DEVELOPMENT_FRAME_APP_INSTALL), DEVELOPMENT_FRAME_FAIL);
    CHECK_EQ(message, "invalid package");
    CHECK(isDirectoryEmptyOrMissing(temporaryDirectory()));

    REQUIRE_EQ(push(*client, "../escape.app", makePackage(), message, DEVELOPMENT_FRAME_APP_INSTALL), DEVELOPMENT_FRAME_FAIL);
    CHECK_EQ(message, "invalid file name");
}

TEST_CASE("each mode only accepts its own requests") {
    ensureAppFixture();
    {
        auto client = openApp();
        client->sendFrame(DEVELOPMENT_FRAME_SYNC_STAT, "/");
        CHECK(client->waitForClose());
    }
    auto client = openSync();
    client->sendFrame(DEVELOPMENT_FRAME_APP_RUN, runnableManifest.id);
    CHECK(client->waitForClose());
}

// endregion

TEST_CASE("trusted clients are listed with their name and can be removed") {
    ensureFixture();
    promptResult = DEVELOPMENT_PROMPT_ACCEPT_ALWAYS;
    std::string status;
    auto client = greetWhenIdle(&identity(4), status, "sync");
    REQUIRE_EQ(status, ACCEPTED);
    client.reset();

    const std::string fingerprint = identity(4).credentials.fingerprint;
    struct Found {
        const std::string& fingerprint;
        std::string name;
        bool found;
    } found { fingerprint, {}, false };
    REQUIRE_EQ(development_server_for_each_trusted_client([](const char* storedFingerprint, const char* name, void* context) {
        auto* found = static_cast<Found*>(context);
        if (found->fingerprint == storedFingerprint) {
            found->found = true;
            found->name = name;
        }
        return true;
    }, &found), ERROR_NONE);
    REQUIRE(found.found);
    CHECK_EQ(found.name, "test");

    REQUIRE_EQ(development_server_remove_trusted_client(fingerprint.c_str()), ERROR_NONE);
    CHECK_EQ(development_server_remove_trusted_client(fingerprint.c_str()), ERROR_NOT_FOUND);

    // The removed client is asked for again
    promptResult = DEVELOPMENT_PROMPT_REJECT;
    auto rejected = greetWhenIdle(&identity(4), status, "sync");
    CHECK_EQ(status, REJECTED);
}
