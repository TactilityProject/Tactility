/*
 * SPDX-FileCopyrightText: 2026 Tactility
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wire format for bridging ESP-NOW across esp-hosted-mcu's custom RPC
 * channel (esp_hosted_send_custom_data / esp_hosted_register_custom_callback).
 * Shared verbatim between slave (runs real esp_now_*) and host (P4, no radio).
 *
 * Kept in sync by hand with the copy in the esp-hosted-mcu fork at
 * common/espnow_bridge/esp_hosted_espnow_bridge_proto.h (Shadowtrance/esp-hosted-mcu,
 * branch feature/espnow-bridge) — this is a wire-format contract between two repos.
 */

#ifndef __ESP_HOSTED_ESPNOW_BRIDGE_PROTO_H
#define __ESP_HOSTED_ESPNOW_BRIDGE_PROTO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* msg_id namespace: any uint32_t except 0xFFFFFFFF (reserved by esp_hosted). */
#define ESPNOW_BRIDGE_MSG_BASE          0xE500U
#define ESPNOW_BRIDGE_REQ_INIT          (ESPNOW_BRIDGE_MSG_BASE + 0)
#define ESPNOW_BRIDGE_RESP_INIT         (ESPNOW_BRIDGE_MSG_BASE + 1)
#define ESPNOW_BRIDGE_REQ_DEINIT        (ESPNOW_BRIDGE_MSG_BASE + 2)
#define ESPNOW_BRIDGE_RESP_DEINIT       (ESPNOW_BRIDGE_MSG_BASE + 3)
#define ESPNOW_BRIDGE_REQ_ADD_PEER      (ESPNOW_BRIDGE_MSG_BASE + 4)
#define ESPNOW_BRIDGE_RESP_ADD_PEER     (ESPNOW_BRIDGE_MSG_BASE + 5)
#define ESPNOW_BRIDGE_REQ_SEND          (ESPNOW_BRIDGE_MSG_BASE + 6)
#define ESPNOW_BRIDGE_RESP_SEND         (ESPNOW_BRIDGE_MSG_BASE + 7)
#define ESPNOW_BRIDGE_EVT_RECV          (ESPNOW_BRIDGE_MSG_BASE + 8)
#define ESPNOW_BRIDGE_EVT_SEND_STATUS   (ESPNOW_BRIDGE_MSG_BASE + 9)

#define ESPNOW_BRIDGE_ETH_ALEN  6
#define ESPNOW_BRIDGE_KEY_LEN   16
/* ESP-NOW v2.0 payload limit (ESP_NOW_MAX_DATA_LEN_V2 in esp_now.h). The slave reports its
 * actual negotiated esp_now_get_version() back to the host in RESP_INIT; hosts talking to a
 * v1.0-only slave still work, they just never send payloads over 250B (native ESP-NOW callers
 * enforce that themselves based on the reported version, same as the non-bridged backend). */
#define ESPNOW_BRIDGE_MAX_DATA_LEN  1470

/* req_init mode: matches tt::service::espnow::Mode / WIFI_MODE_STA vs WIFI_MODE_AP on the slave */
#define ESPNOW_BRIDGE_MODE_STATION       0U
#define ESPNOW_BRIDGE_MODE_ACCESS_POINT  1U

/* req: ESPNOW_BRIDGE_REQ_INIT */
typedef struct __attribute__((packed)) {
    uint8_t pmk[ESPNOW_BRIDGE_KEY_LEN];
    uint8_t channel;      /* 0 = use current STA/AP channel */
    uint8_t long_range;   /* bool as uint8_t: fixed 1-byte width for this hand-synced wire struct */
    uint8_t mode;         /* ESPNOW_BRIDGE_MODE_STATION / ESPNOW_BRIDGE_MODE_ACCESS_POINT: which WiFi
                            * mode+interface the slave should bring up and register ESP-NOW against */
} espnow_bridge_req_init_t;

/* resp: ESPNOW_BRIDGE_RESP_DEINIT / RESP_ADD_PEER / RESP_SEND */
typedef struct __attribute__((packed)) {
    int32_t esp_err;   /* raw esp_err_t from the slave-side call */
} espnow_bridge_resp_status_t;

/* resp: ESPNOW_BRIDGE_RESP_INIT */
typedef struct __attribute__((packed)) {
    int32_t esp_err;        /* raw esp_err_t from the slave-side esp_now_init() call */
    uint32_t espnow_version; /* esp_now_get_version() result, 0 if esp_err != ESP_OK */
} espnow_bridge_resp_init_t;

/* req: ESPNOW_BRIDGE_REQ_ADD_PEER */
typedef struct __attribute__((packed)) {
    uint8_t peer_addr[ESPNOW_BRIDGE_ETH_ALEN];
    uint8_t lmk[ESPNOW_BRIDGE_KEY_LEN];
    uint8_t channel;
    uint8_t encrypt;      /* bool as uint8_t: fixed 1-byte width for this hand-synced wire struct */
    uint8_t ifidx;        /* ESPNOW_BRIDGE_MODE_STATION / ESPNOW_BRIDGE_MODE_ACCESS_POINT: which
                            * interface (WIFI_IF_STA / WIFI_IF_AP) to bind the peer to on the slave,
                            * matching esp_now_peer_info_t::ifidx on native */
} espnow_bridge_req_add_peer_t;

/* req: ESPNOW_BRIDGE_REQ_SEND */
typedef struct __attribute__((packed)) {
    uint8_t dest_addr[ESPNOW_BRIDGE_ETH_ALEN];
    uint8_t broadcast;    /* bool as uint8_t; true: dest_addr ignored, esp_now_send(NULL, ...) */
    uint16_t data_len;
    uint8_t data[ESPNOW_BRIDGE_MAX_DATA_LEN];
} espnow_bridge_req_send_t;

/* event: ESPNOW_BRIDGE_EVT_RECV (slave-initiated, unsolicited) */
typedef struct __attribute__((packed)) {
    uint8_t src_addr[ESPNOW_BRIDGE_ETH_ALEN];
    uint8_t des_addr[ESPNOW_BRIDGE_ETH_ALEN];
    int8_t rssi;
    uint8_t channel;
    uint16_t data_len;
    uint8_t data[ESPNOW_BRIDGE_MAX_DATA_LEN];
} espnow_bridge_evt_recv_t;

/* event: ESPNOW_BRIDGE_EVT_SEND_STATUS (slave-initiated, unsolicited) */
typedef struct __attribute__((packed)) {
    uint8_t peer_addr[ESPNOW_BRIDGE_ETH_ALEN];
    uint8_t success;      /* bool as uint8_t: fixed 1-byte width for this hand-synced wire struct */
} espnow_bridge_evt_send_status_t;

#ifdef __cplusplus
}
#endif

#endif /* __ESP_HOSTED_ESPNOW_BRIDGE_PROTO_H */
