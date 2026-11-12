// SPDX-License-Identifier: Apache-2.0
#include <development/session.h>

#include <tactility/freertos/task.h>
#include <tactility/memory.h>

#include <mbedtls/net_sockets.h>

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {

constexpr MemoryPolicy MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
constexpr uint32_t STATUS_TIMEOUT_MS = 5000;
constexpr uint32_t FRAME_TIMEOUT_MS = 5000;
constexpr TickType_t WRITE_RETRY_DELAY = pdMS_TO_TICKS(5);

int bioSend(void* context, const unsigned char* data, size_t length) {
    const int socket = *static_cast<int*>(context);
    const ssize_t sent = send(socket, data, length, MSG_NOSIGNAL);
    if (sent >= 0) {
        return static_cast<int>(sent);
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

int bioReceive(void* context, unsigned char* data, size_t length) {
    const int socket = *static_cast<int*>(context);
    const ssize_t received = recv(socket, data, length, 0);
    if (received > 0) {
        return static_cast<int>(received);
    }
    if (received == 0) {
        return MBEDTLS_ERR_NET_CONN_RESET;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

} // namespace

DevelopmentSession* development_session_alloc(int socket, const mbedtls_ssl_config* config) {
    auto* session = static_cast<DevelopmentSession*>(memory_calloc_with_policy(1, sizeof(DevelopmentSession), &MEMORY_POLICY));
    if (session == nullptr) {
        return nullptr;
    }
    session->socket = socket;
    mbedtls_ssl_init(&session->ssl);
    if (mbedtls_ssl_setup(&session->ssl, config) != 0) {
        mbedtls_ssl_free(&session->ssl);
        memory_free(session);
        return nullptr;
    }
    mbedtls_ssl_set_bio(&session->ssl, &session->socket, bioSend, bioReceive, nullptr);
    return session;
}

void development_session_free(DevelopmentSession* session) {
    mbedtls_ssl_close_notify(&session->ssl);
    mbedtls_ssl_free(&session->ssl);
    close(session->socket);
    memory_free(session);
}

bool development_session_write(DevelopmentSession* session, const void* data, size_t length, uint32_t timeout_ms) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    const TickType_t start = xTaskGetTickCount();
    size_t offset = 0;
    while (offset < length) {
        const int result = mbedtls_ssl_write(&session->ssl, bytes + offset, length - offset);
        if (result > 0) {
            offset += static_cast<size_t>(result);
        } else if (result == MBEDTLS_ERR_SSL_WANT_WRITE || result == MBEDTLS_ERR_SSL_WANT_READ) {
            if (xTaskGetTickCount() - start > pdMS_TO_TICKS(timeout_ms)) {
                return false;
            }
            vTaskDelay(WRITE_RETRY_DELAY);
        } else {
            return false;
        }
    }
    return true;
}

bool development_session_send_status(DevelopmentSession* session, const char* status, const char* message) {
    char line[256];
    const size_t length = development_protocol_format_status(status, message, line, sizeof(line));
    return length > 0 && development_session_write(session, line, length, STATUS_TIMEOUT_MS);
}

bool development_session_send_info(DevelopmentSession* session, const char* vendor, const char* name, const char* id, const char* version) {
    char line[256];
    const size_t length = development_protocol_format_info(vendor, name, id, version, line, sizeof(line));
    return length > 0 && development_session_write(session, line, length, STATUS_TIMEOUT_MS);
}

bool development_session_send_frame(DevelopmentSession* session, DevelopmentFrameType type, const void* payload, uint16_t length) {
    if (length > DEVELOPMENT_FRAME_PAYLOAD_MAX) {
        return false;
    }
    development_frame_write_header(type, length, session->transmit);
    if (length > 0) {
        memcpy(session->transmit + DEVELOPMENT_FRAME_HEADER_SIZE, payload, length);
    }
    return development_session_write(session, session->transmit, DEVELOPMENT_FRAME_HEADER_SIZE + length, FRAME_TIMEOUT_MS);
}
