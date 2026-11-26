// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <http/private/http_parsing.h>

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <functional>
#include <string>

/**
 * A connection to a server, plain or TLS. For platforms with BSD sockets.
 * The session, the socket and the certificates are released on destruction.
 */
class HttpPosixConnection {
    int fd = -1;
    bool secure = true;
    bool handshakeDone = false;
    bool certificateRejected = false;
    mbedtls_x509_crt ca {};
    mbedtls_ssl_config config {};
    mbedtls_ssl_context ssl {};
    // Bytes that readLine() read beyond the line it returned
    std::string pending;

    /** @return the number of bytes read, 0 when the server closed the connection, or -1 on an error */
    int receive(char* buffer, size_t size);

public:
    HttpPosixConnection();
    ~HttpPosixConnection();
    HttpPosixConnection(const HttpPosixConnection&) = delete;
    HttpPosixConnection& operator=(const HttpPosixConnection&) = delete;

    /** Trusts the PEM certificate, or the operating system's certificates when it is empty */
    bool loadCertificate(const std::string& certPem);

    bool connectSocket(const HttpUrl& url, uint32_t timeoutMs);

    bool handshake(const HttpUrl& url, const std::function<bool()>& isCancelled);

    /** True when the handshake failed because the server's certificate could not be verified */
    bool isCertificateRejected() const { return certificateRejected; }

    bool writeAll(const std::string& data);

    /** @return the number of bytes read, 0 when the server closed the connection, or -1 on an error */
    int read(char* buffer, size_t size);

    /** Reads a line that ends with CRLF, without the CRLF */
    bool readLine(std::string& line);
};
