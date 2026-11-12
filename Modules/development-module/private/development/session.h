// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <development/protocol.h>
#include <development/server.h>

#include <app/manifest.h>
#include <service/manifest.h>

#include <mbedtls/ssl.h>

#include <stddef.h>
#include <stdint.h>

#define DEVELOPMENT_SESSION_APP_ID "development-session"

/** Shell sessions are limited to 1, as each one also runs a shell app */
#define DEVELOPMENT_SERVER_MAX_SESSIONS 3

/** An accepted TLS connection whose client sent a valid hello */
struct DevelopmentSession {
    int socket;
    mbedtls_ssl_context ssl;
    /** Set by the TLS verify callback, empty when the client sent no certificate */
    char fingerprint[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1];
    char peer_address[16];
    DevelopmentHello hello;
    bool trusted;
    /** Index of the server slot that tracks this session */
    int slot;
    /** Holds a whole frame, so it's sent as a single TLS record */
    uint8_t transmit[DEVELOPMENT_FRAME_HEADER_SIZE + DEVELOPMENT_FRAME_PAYLOAD_MAX];
};

extern "C" const struct AppManifest development_session_app_manifest;

extern "C" const struct ServiceManifest development_server_service_manifest;

/** @return a session that wraps @a socket, or NULL when out of memory */
DevelopmentSession* development_session_alloc(int socket, const mbedtls_ssl_config* config);

/** Sends a TLS close_notify when possible, then releases the session and its socket */
void development_session_free(DevelopmentSession* session);

/** Writes all of @a data, waiting at most @a timeout_ms when the socket is not writable */
bool development_session_write(DevelopmentSession* session, const void* data, size_t length, uint32_t timeout_ms);

bool development_session_send_status(DevelopmentSession* session, const char* status, const char* message);

/** Sends the "accepted" line of an "info" hello, with the device information */
bool development_session_send_info(DevelopmentSession* session, const char* vendor, const char* name, const char* id, const char* version);

bool development_session_send_frame(DevelopmentSession* session, DevelopmentFrameType type, const void* payload, uint16_t length);

/** Hands the session that the listener accepted in @a slot to its session app. @return NULL when there is none */
DevelopmentSession* development_server_take_session(int slot);

/** Releases a session from development_server_take_session(), so the server accepts a new one */
void development_server_finish_session(DevelopmentSession* session);

/** Accepts future connections that use the certificate with @a fingerprint without asking */
error_t development_server_trust(const char* fingerprint, const char* name);

/** Sets the port for the next time the server starts listening. For testing purposes. */
void development_server_set_port(uint16_t port);
