// SPDX-License-Identifier: Apache-2.0
#include <http/private/posix_connection.h>

#include <psa/crypto.h>

#include <tactility/log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

constexpr auto* TAG = "http-connection";
constexpr size_t BUFFER_SIZE = 512;
constexpr const char* SYSTEM_CERTIFICATE_FILES[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/cert.pem",
};

// A write to a closed connection must fail instead of raising SIGPIPE. macOS has no MSG_NOSIGNAL, but SO_NOSIGPIPE on the socket.
#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

int bio_send(void* context, const unsigned char* data, size_t size) {
    const int fd = *static_cast<int*>(context);
    const ssize_t sent = send(fd, data, size, SEND_FLAGS);
    if (sent >= 0) {
        return static_cast<int>(sent);
    }
    if (errno == EINTR) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_TIMEOUT : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

int bio_receive(void* context, unsigned char* data, size_t size) {
    const int fd = *static_cast<int*>(context);
    const ssize_t received = recv(fd, data, size, 0);
    if (received >= 0) {
        return static_cast<int>(received);
    }
    if (errno == EINTR) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    // The socket's receive timeout elapsed
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_TIMEOUT : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

}

HttpPosixConnection::HttpPosixConnection() {
    mbedtls_x509_crt_init(&ca);
    mbedtls_ssl_config_init(&config);
    mbedtls_ssl_init(&ssl);
}

HttpPosixConnection::~HttpPosixConnection() {
    if (handshakeDone) {
        mbedtls_ssl_close_notify(&ssl);
    }
    if (fd >= 0) {
        close(fd);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_x509_crt_free(&ca);
}

bool HttpPosixConnection::loadCertificate(const std::string& certPem) {
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    if (!certPem.empty()) {
        // A PEM certificate is parsed as a NUL-terminated string
        return mbedtls_x509_crt_parse(&ca, reinterpret_cast<const unsigned char*>(certPem.c_str()), certPem.size() + 1) == 0;
    }
    for (const char* path : SYSTEM_CERTIFICATE_FILES) {
        if (mbedtls_x509_crt_parse_file(&ca, path) >= 0) {
            return true;
        }
    }
    LOG_E(TAG, "No system certificates found");
    return false;
}

bool HttpPosixConnection::connectSocket(const HttpUrl& url, uint32_t timeoutMs) {
    secure = url.secure;
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &addresses) != 0) {
        LOG_E(TAG, "Failed to resolve %s", url.host.c_str());
        return false;
    }

    const timeval timeout {
        .tv_sec = static_cast<time_t>(timeoutMs / 1000),
        .tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000)
    };
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            continue;
        }
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        int result;
        do {
            result = ::connect(fd, address->ai_addr, address->ai_addrlen);
        } while (result != 0 && errno == EINTR);
        if (result == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd >= 0;
}

bool HttpPosixConnection::handshake(const HttpUrl& url, const std::function<bool()>& isCancelled) {
    if (!secure) {
        return true;
    }
    if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        return false;
    }
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&config, &ca, nullptr);
    // Same as the ESP32 implementation
    mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
    if (mbedtls_ssl_setup(&ssl, &config) != 0 || mbedtls_ssl_set_hostname(&ssl, url.host.c_str()) != 0) {
        return false;
    }
    mbedtls_ssl_set_bio(&ssl, &fd, bio_send, bio_receive, nullptr);

    int result;
    while ((result = mbedtls_ssl_handshake(&ssl)) != 0) {
        if ((result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) || isCancelled()) {
            certificateRejected = result == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            LOG_E(TAG, "TLS handshake with %s failed: -0x%04x", url.host.c_str(), static_cast<unsigned>(-result));
            return false;
        }
    }
    handshakeDone = true;
    return true;
}

bool HttpPosixConnection::writeAll(const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        if (!secure) {
            const ssize_t sent = send(fd, data.data() + offset, data.size() - offset, SEND_FLAGS);
            if (sent > 0) {
                offset += static_cast<size_t>(sent);
            } else if (errno != EINTR) {
                return false;
            }
            continue;
        }
        const int written = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(data.data()) + offset, data.size() - offset);
        if (written > 0) {
            offset += static_cast<size_t>(written);
        } else if (written != MBEDTLS_ERR_SSL_WANT_READ && written != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return false;
        }
    }
    return true;
}

int HttpPosixConnection::receive(char* buffer, size_t size) {
    while (true) {
        if (!secure) {
            const ssize_t received = recv(fd, buffer, size, 0);
            if (received >= 0) {
                return static_cast<int>(received);
            }
            if (errno != EINTR) {
                return -1;
            }
            continue;
        }
        const int result = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(buffer), size);
        if (result >= 0) {
            return result;
        }
        if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return 0;
        }
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
    }
}

int HttpPosixConnection::read(char* buffer, size_t size) {
    if (!pending.empty()) {
        const size_t count = std::min(size, pending.size());
        memcpy(buffer, pending.data(), count);
        pending.erase(0, count);
        return static_cast<int>(count);
    }
    return receive(buffer, size);
}

bool HttpPosixConnection::readLine(std::string& line) {
    constexpr size_t MAX_LINE_LENGTH = 8192;
    while (true) {
        const auto end = pending.find("\r\n");
        if (end != std::string::npos) {
            if (end > MAX_LINE_LENGTH) {
                return false;
            }
            line = pending.substr(0, end);
            pending.erase(0, end + 2);
            return true;
        }
        // A line of the maximum length can have received only the CR of its terminator
        if (pending.size() > MAX_LINE_LENGTH && !(pending.size() == MAX_LINE_LENGTH + 1 && pending.back() == '\r')) {
            return false;
        }
        char buffer[BUFFER_SIZE];
        const int result = receive(buffer, sizeof(buffer));
        if (result <= 0) {
            return false;
        }
        pending.append(buffer, static_cast<size_t>(result));
    }
}
