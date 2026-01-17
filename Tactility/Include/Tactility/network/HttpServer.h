#pragma once
#ifdef ESP_PLATFORM

#include <esp_http_server.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/kernel/SystemEvents.h>

namespace tt::network {

class HttpServer {

public:

    /**
     * @brief  Function for URI matching used by server.
     *
     * @param[in] referenceUri   URI/template with respect to which the other URI is matched
     * @param[in] uriToCheck     URI/template being matched to the reference URI/template
     * @param[in] matchUpTo      For specifying the actual length of `uri_to_match` up to
     *                            which the matching algorithm is to be applied (The maximum
     *                            value is `strlen(uri_to_match)`, independent of the length
     *                            of `reference_uri`)
     * @return true on match
     */
    typedef bool (*UriMatchFunction)(const char* referenceUri, const char* uriToCheck, size_t matchUpTo);

private:

    const uint32_t port;
    const std::string address;
    const uint32_t stackSize;
    const UriMatchFunction matchUri;

    std::vector<httpd_uri_t> handlers;

    RecursiveMutex mutex;
    httpd_handle_t server = nullptr;

    bool startInternal();
    void stopInternal();

public:

    HttpServer(
        uint32_t port,
        const std::string& address,
        std::vector<httpd_uri_t> handlers,
        uint32_t stackSize = 5120,
        UriMatchFunction matchUri = httpd_uri_match_wildcard
    ) :
        port(port),
        address(address),
        stackSize(stackSize),
        matchUri(matchUri),
        handlers(std::move(handlers))
    {}

    bool start();

    void stop();

    bool isStarted() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return server != nullptr;
    }
};

}

#endif