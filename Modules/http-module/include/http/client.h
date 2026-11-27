// SPDX-License-Identifier: Apache-2.0
#pragma once

/**
 * A streaming HTTP(S) GET client.
 *
 * All calls block and belong on one task, except http_client_cancel().
 * HTTPS servers are verified against the trusted root certificates of the platform: the certificate bundle on ESP32,
 * the system certificates on the simulator. http_client_set_certificate() replaces them with one root certificate.
 * Redirects are not followed: read the "location" header and call http_client_open() again.
 */

#include <tactility/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Why a request failed */
enum HttpClientFailure {
    HTTP_CLIENT_FAILURE_NONE = 0,
    HTTP_CLIENT_FAILURE_URL, // not a valid http:// or https:// URL
    HTTP_CLIENT_FAILURE_CONNECT, // name lookup, connection or TLS handshake
    HTTP_CLIENT_FAILURE_CERTIFICATE, // the server's certificate could not be verified
    HTTP_CLIENT_FAILURE_PROTOCOL, // the server sent an invalid or incomplete response
    HTTP_CLIENT_FAILURE_CANCELLED,
    HTTP_CLIENT_FAILURE_MEMORY,
    HTTP_CLIENT_FAILURE_NOT_OPEN, // there is no open response to read
};

struct HttpClient;

/** @return NULL when out of memory */
struct HttpClient* http_client_alloc(void);

/** Closes the connection and frees the client. NULL is ignored. */
void http_client_free(struct HttpClient* client);

/**
 * Adds a header to the requests that follow, replacing one with the same name. Both strings are copied.
 * "Host" and "Connection" are set by the client itself.
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT the name or value contains characters that are not allowed in a header
 */
error_t http_client_set_header(struct HttpClient* client, const char* name, const char* value);

/** The time to wait for the server per step (connect, read, write). The default is 10 seconds. */
void http_client_set_timeout(struct HttpClient* client, uint32_t timeout_ms);

/**
 * Verifies the servers of the requests that follow with one root certificate instead of the trusted roots of the platform.
 * @param[in] certificate_pem PEM text that must stay valid while the client is used, or NULL for the trusted roots of the platform
 */
void http_client_set_certificate(struct HttpClient* client, const char* certificate_pem);

/**
 * Sends a GET request and waits for the status and headers of the response. Closes any response that was open.
 * Any status code counts as success.
 * @retval ERROR_NONE a response arrived
 * @retval ERROR_INVALID_ARGUMENT the URL is not valid
 * @retval ERROR_RESOURCE the request failed, see http_client_get_failure()
 */
error_t http_client_open(struct HttpClient* client, const char* url);

/** @return the status code of the open response, or 0 */
int http_client_get_status_code(struct HttpClient* client);

/** @return the length of the body, or -1 when the server didn't say */
int64_t http_client_get_content_length(struct HttpClient* client);

/**
 * @param[in] name case-insensitive
 * @return the value of a response header, or NULL. Valid until the next open, close or free.
 */
const char* http_client_get_header(struct HttpClient* client, const char* name);

/**
 * Reads the next part of the body, without any chunked transfer encoding.
 * @return the number of bytes read, 0 at the end of the body, or -1 on failure (including a body that ended early)
 */
int http_client_read(struct HttpClient* client, void* buffer, size_t size);

/** Why the last open or read failed */
enum HttpClientFailure http_client_get_failure(struct HttpClient* client);

/**
 * Makes the current and following calls fail with HTTP_CLIENT_FAILURE_CANCELLED. Safe to call from any task.
 * A call that is waiting for the server returns when data arrives or its timeout passes.
 * The client stays cancelled until http_client_reset().
 */
void http_client_cancel(struct HttpClient* client);

/** Clears a cancellation, so that the client can be used again */
void http_client_reset(struct HttpClient* client);

/** Closes the connection of the open response. Request headers are kept. */
void http_client_close(struct HttpClient* client);

#ifdef __cplusplus
}
#endif
