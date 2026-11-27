// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <http/client.h>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

/** The platform's part of an HttpClient: one request and its response */
class HttpClientBackend {
public:
    virtual ~HttpClientBackend() = default;

    /** Sends the request and fills in the status code, content length and response headers */
    virtual HttpClientFailure open(struct HttpClient& client, const std::string& url) = 0;

    /** See http_client_read() */
    virtual int read(struct HttpClient& client, void* buffer, size_t size) = 0;
};

using HttpHeaders = tt::std::vector<std::pair<tt::std::string, tt::std::string>>;

struct HttpClient {
    std::unique_ptr<HttpClientBackend> backend;
    HttpHeaders requestHeaders;
    /** Names in lower case */
    HttpHeaders responseHeaders;
    size_t responseHeaderBytes = 0;
    size_t framingHeaderBytes = 0;
    /** A header that frames the body didn't fit, so the response can't be read */
    bool framingHeaderDropped = false;
    int statusCode = 0;
    int64_t contentLength = -1;
    uint32_t timeoutMs = 10000;
    /** NULL for the trusted roots of the platform */
    const char* certificatePem = nullptr;
    HttpClientFailure failure = HTTP_CLIENT_FAILURE_NONE;
    std::atomic<bool> cancelled = false;

    /** Keeps a response header, up to a total size limit. The headers that frame the body have a limit of their own. Repeated headers are joined with ", ". */
    void addResponseHeader(const std::string& name, const std::string& value);

    void clearResponseHeaders();
};

/** Implemented per platform */
std::unique_ptr<HttpClientBackend> http_client_create_backend();
