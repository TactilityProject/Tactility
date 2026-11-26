// SPDX-License-Identifier: Apache-2.0
#include <http/private/client.h>
#include <http/private/http_parsing.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

namespace {

constexpr size_t MAX_RESPONSE_HEADER_BYTES = 8192;
constexpr size_t MAX_FRAMING_HEADER_BYTES = 256;

std::string to_lower(std::string text) {
    for (auto& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

}

void HttpClient::addResponseHeader(const std::string& name, const std::string& value) {
    const std::string lowerName = to_lower(name);
    const bool framing = lowerName == "content-length" || lowerName == "transfer-encoding";
    size_t& usedBytes = framing ? framingHeaderBytes : responseHeaderBytes;
    if (usedBytes + name.size() + value.size() > (framing ? MAX_FRAMING_HEADER_BYTES : MAX_RESPONSE_HEADER_BYTES)) {
        framingHeaderDropped = framingHeaderDropped || framing;
        return;
    }
    usedBytes += name.size() + value.size();
    for (auto& header : responseHeaders) {
        if (header.first == lowerName) {
            header.second += ", " + value;
            return;
        }
    }
    responseHeaders.emplace_back(lowerName, value);
}

void HttpClient::clearResponseHeaders() {
    responseHeaders.clear();
    responseHeaderBytes = 0;
    framingHeaderBytes = 0;
    framingHeaderDropped = false;
}

extern "C" {

HttpClient* http_client_alloc() {
    return new (std::nothrow) HttpClient();
}

void http_client_free(HttpClient* client) {
    delete client;
}

error_t http_client_set_header(HttpClient* client, const char* name, const char* value) {
    if (!http_is_valid_header_name(name) || !http_is_valid_header_value(value)) {
        return ERROR_INVALID_ARGUMENT;
    }
    const std::string lowerName = to_lower(name);
    if (lowerName == "host" || lowerName == "connection") {
        return ERROR_INVALID_ARGUMENT;
    }
    for (auto& header : client->requestHeaders) {
        if (to_lower(header.first) == lowerName) {
            header.second = value;
            return ERROR_NONE;
        }
    }
    client->requestHeaders.emplace_back(name, value);
    return ERROR_NONE;
}

void http_client_set_timeout(HttpClient* client, uint32_t timeout_ms) {
    client->timeoutMs = timeout_ms;
}

void http_client_close(HttpClient* client) {
    client->backend = nullptr;
    client->clearResponseHeaders();
    client->statusCode = 0;
    client->contentLength = -1;
}

error_t http_client_open(HttpClient* client, const char* url) {
    http_client_close(client);
    client->failure = HTTP_CLIENT_FAILURE_NONE;

    HttpUrl parsed;
    if (url == nullptr || !http_parse_url(url, parsed)) {
        client->failure = HTTP_CLIENT_FAILURE_URL;
        return ERROR_INVALID_ARGUMENT;
    }
    if (client->cancelled) {
        client->failure = HTTP_CLIENT_FAILURE_CANCELLED;
        return ERROR_RESOURCE;
    }

    client->backend = http_client_create_backend();
    HttpClientFailure failure = client->backend == nullptr ? HTTP_CLIENT_FAILURE_MEMORY : client->backend->open(*client, url);
    if (failure == HTTP_CLIENT_FAILURE_NONE) {
        if (client->cancelled) {
            failure = HTTP_CLIENT_FAILURE_CANCELLED;
        } else if (client->framingHeaderDropped) {
            failure = HTTP_CLIENT_FAILURE_PROTOCOL;
        }
    }
    if (failure != HTTP_CLIENT_FAILURE_NONE) {
        http_client_close(client);
        client->failure = client->cancelled ? HTTP_CLIENT_FAILURE_CANCELLED : failure;
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

int http_client_get_status_code(HttpClient* client) {
    return client->statusCode;
}

int64_t http_client_get_content_length(HttpClient* client) {
    return client->contentLength;
}

const char* http_client_get_header(HttpClient* client, const char* name) {
    const std::string lowerName = to_lower(name);
    for (const auto& header : client->responseHeaders) {
        if (header.first == lowerName) {
            return header.second.c_str();
        }
    }
    return nullptr;
}

int http_client_read(HttpClient* client, void* buffer, size_t size) {
    if (client->cancelled) {
        client->failure = HTTP_CLIENT_FAILURE_CANCELLED;
        return -1;
    }
    if (client->backend == nullptr) {
        client->failure = HTTP_CLIENT_FAILURE_NOT_OPEN;
        return -1;
    }
    const int result = client->backend->read(*client, buffer, std::min<size_t>(size, INT32_MAX));
    if (client->cancelled) {
        // The bytes that were consumed are lost, so the response can't be read any further
        client->backend = nullptr;
        client->failure = HTTP_CLIENT_FAILURE_CANCELLED;
        return -1;
    }
    if (result < 0) {
        client->failure = HTTP_CLIENT_FAILURE_PROTOCOL;
    }
    return result;
}

HttpClientFailure http_client_get_failure(HttpClient* client) {
    return client->failure;
}

void http_client_cancel(HttpClient* client) {
    client->cancelled = true;
}

void http_client_reset(HttpClient* client) {
    client->cancelled = false;
}

}
