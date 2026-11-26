// SPDX-License-Identifier: Apache-2.0
#include <http/private/client.h>
#include <http/private/http_parsing.h>
#include <http/private/posix_connection.h>

#include <algorithm>
#include <new>

namespace {

class PosixBackend final : public HttpClientBackend {
    HttpPosixConnection connection;
    bool chunked = false;
    bool done = false;
    // Bytes left of the current chunk, or of the body when it isn't chunked. -1 means until the server closes.
    long long remaining = -1;
    bool inChunk = false;

    HttpClientFailure readHead(HttpClient& client) {
        std::string line;
        // Interim responses (1xx) come before the real one
        do {
            if (!connection.readLine(line) || !http_parse_status_line(line, client.statusCode)) {
                return HTTP_CLIENT_FAILURE_PROTOCOL;
            }
            std::string name;
            std::string value;
            size_t headerBytes = 0;
            while (true) {
                if (!connection.readLine(line) || client.cancelled) {
                    return HTTP_CLIENT_FAILURE_PROTOCOL;
                }
                if (line.empty()) {
                    break;
                }
                // Stops a server that never ends its headers
                headerBytes += line.size();
                if (headerBytes > 65536) {
                    return HTTP_CLIENT_FAILURE_PROTOCOL;
                }
                if (client.statusCode >= 200 && http_parse_header_line(line, name, value)) {
                    client.addResponseHeader(name, value);
                }
            }
        } while (client.statusCode < 200);
        return HTTP_CLIENT_FAILURE_NONE;
    }

    // The optional trailers after the last chunk end with an empty line
    bool readTrailers(const HttpClient& client) {
        std::string line;
        size_t trailerBytes = 0;
        while (connection.readLine(line) && !client.cancelled) {
            if (line.empty()) {
                return true;
            }
            trailerBytes += line.size();
            if (trailerBytes > 8192) {
                break;
            }
        }
        return false;
    }

public:
    HttpClientFailure open(HttpClient& client, const std::string& url) override {
        HttpUrl parsed;
        if (!http_parse_url(url, parsed)) {
            return HTTP_CLIENT_FAILURE_URL;
        }
        if (parsed.secure && !connection.loadCertificate("")) {
            return HTTP_CLIENT_FAILURE_CERTIFICATE;
        }
        if (!connection.connectSocket(parsed, client.timeoutMs)) {
            return HTTP_CLIENT_FAILURE_CONNECT;
        }
        if (!connection.handshake(parsed, [&client] { return client.cancelled.load(); })) {
            return connection.isCertificateRejected() ? HTTP_CLIENT_FAILURE_CERTIFICATE : HTTP_CLIENT_FAILURE_CONNECT;
        }

        const bool defaultPort = parsed.port == (parsed.secure ? "443" : "80");
        const bool ipv6 = parsed.host.find(':') != std::string::npos;
        std::string request = "GET " + parsed.path + " HTTP/1.1\r\nHost: " + (ipv6 ? "[" + parsed.host + "]" : parsed.host) +
            (defaultPort ? "" : ":" + parsed.port) + "\r\nConnection: close\r\n";
        for (const auto& header : client.requestHeaders) {
            request.append(header.first).append(": ").append(header.second).append("\r\n");
        }
        request += "\r\n";
        if (!connection.writeAll(request)) {
            return HTTP_CLIENT_FAILURE_CONNECT;
        }

        const HttpClientFailure failure = readHead(client);
        if (failure != HTTP_CLIENT_FAILURE_NONE) {
            return failure;
        }

        const char* encoding = http_client_get_header(&client, "transfer-encoding");
        chunked = encoding != nullptr && http_is_chunked(encoding);
        const char* length = http_client_get_header(&client, "content-length");
        // A chunked body ignores the Content-Length
        if (!chunked && length != nullptr) {
            long parsedLength = 0;
            if (!http_parse_content_length(length, parsedLength)) {
                return HTTP_CLIENT_FAILURE_PROTOCOL;
            }
            client.contentLength = parsedLength;
        }
        remaining = chunked ? 0 : client.contentLength;
        done = client.statusCode == 204 || client.statusCode == 304 || client.contentLength == 0;
        return HTTP_CLIENT_FAILURE_NONE;
    }

    int read(HttpClient& client, void* buffer, size_t size) override {
        if (done || size == 0) {
            return 0;
        }
        if (chunked && remaining == 0) {
            std::string line;
            // A chunk's data is followed by an empty line
            if (inChunk && (!connection.readLine(line) || !line.empty())) {
                return -1;
            }
            size_t chunkSize = 0;
            if (!connection.readLine(line) || !http_parse_chunk_size(line, chunkSize)) {
                return -1;
            }
            if (chunkSize == 0) {
                if (!readTrailers(client)) {
                    return -1;
                }
                done = true;
                return 0;
            }
            remaining = static_cast<long long>(chunkSize);
            inChunk = true;
        }

        const size_t wanted = remaining < 0 ? size : static_cast<size_t>(std::min<unsigned long long>(size, remaining));
        const int received = connection.read(static_cast<char*>(buffer), wanted);
        if (received < 0) {
            return -1;
        }
        if (received == 0) {
            done = remaining < 0;
            return done ? 0 : -1;
        }
        if (remaining > 0) {
            remaining -= received;
            done = !chunked && remaining == 0;
        }
        return received;
    }
};

}

std::unique_ptr<HttpClientBackend> http_client_create_backend() {
    return std::unique_ptr<HttpClientBackend>(new (std::nothrow) PosixBackend());
}
