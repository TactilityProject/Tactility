// ble_hci_gate.c — Strong override of the weak hci_rx_handler from vhci_drv.c.
// Gates HCI packet delivery so packets are silently dropped when NimBLE is not
// initialised, preventing null-npl_funcs crashes during radio teardown (P4 only).
//
// On ESP32-P4, sdio_process_rx_task runs independently of the NimBLE host and can
// deliver an HCI packet after nimble_port_deinit() has zeroed npl_funcs.
// dispatch_disable() calls ble_hci_gate_set_active(false) + ble_hci_gate_wait_idle()
// before touching NimBLE, ensuring no in-flight or future hci_rx_handler call can
// dereference NimBLE internals.

#include <sdkconfig.h>
#if defined(CONFIG_BT_NIMBLE_ENABLED) && defined(CONFIG_ESP_HOSTED_ENABLED)

#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// NimBLE transport + HCI
#include <nimble/transport.h>
#include <nimble/hci_common.h>
#include <os/os_mbuf.h>

// H4 packet type indicators (same values used in vhci_drv.c)
#define HCI_H4_EVT  0x04
#define HCI_H4_ACL  0x02

// Local defines from vhci_drv.c (not exported by any header)
#define BLE_HCI_EVENT_HDR_LEN  (2)

static const char* TAG = "ble_hci_gate";

// ---- Gate state (zero-init = gate closed until dispatch_enable opens it) ----
static atomic_bool s_hci_active   = ATOMIC_VAR_INIT(false);
static atomic_int  s_hci_refcount = ATOMIC_VAR_INIT(0);

void ble_hci_gate_set_active(bool active) {
    atomic_store_explicit(&s_hci_active, active, memory_order_seq_cst);
}

// Spin-wait until all in-flight hci_rx_handler calls complete (max_ms timeout).
// Returns true if drained within the timeout.
bool ble_hci_gate_wait_idle(int max_ms) {
    for (int elapsed = 0; elapsed < max_ms; elapsed++) {
        if (atomic_load_explicit(&s_hci_refcount, memory_order_acquire) == 0)
            return true;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return (atomic_load_explicit(&s_hci_refcount, memory_order_acquire) == 0);
}

// Strong override — replaces the H_WEAK_REF hci_rx_handler in vhci_drv.c.
// All four esp-hosted transport drivers (SDIO, SPI, SPI-HD, UART) call this symbol.
int hci_rx_handler(uint8_t *buf, size_t buf_len) {
    // Fast path: gate closed — NimBLE not ready, drop silently
    if (!atomic_load_explicit(&s_hci_active, memory_order_acquire))
        return ESP_OK;

    // Hold a reference so dispatch_disable() waits for us to finish
    atomic_fetch_add_explicit(&s_hci_refcount, 1, memory_order_acquire);

    // Double-check: gate may have closed between the first check and the increment
    if (!atomic_load_explicit(&s_hci_active, memory_order_acquire)) {
        atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
        return ESP_OK;
    }

    // ---- Original vhci_drv.c (NimBLE branch) logic ----
    uint8_t *data = buf;
    uint32_t len_total_read = buf_len;
    int rc;

    if (data[0] == HCI_H4_EVT) {
        uint8_t *evbuf;
        int totlen = BLE_HCI_EVENT_HDR_LEN + data[2];

        if (totlen > UINT8_MAX + BLE_HCI_EVENT_HDR_LEN) {
            ESP_LOGE(TAG, "Rx: len[%d] > max INT, drop", totlen);
            atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
            return ESP_FAIL;
        }
        if (totlen > MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE)) {
            ESP_LOGE(TAG, "Rx: len[%d] > max BLE, drop", totlen);
            atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
            return ESP_FAIL;
        }
        if (data[1] == BLE_HCI_EVCODE_HW_ERROR) {
            ESP_LOGE(TAG, "Rx: HW_ERROR");
            atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
            return ESP_FAIL;
        }

        if ((data[1] == BLE_HCI_EVCODE_LE_META) &&
            (data[3] == BLE_HCI_LE_SUBEV_ADV_RPT ||
             data[3] == BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
            evbuf = ble_transport_alloc_evt(1);
            if (!evbuf) {
                ESP_LOGW(TAG, "Rx: Drop ADV Report: OOM (not fatal)");
                atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
                return ESP_FAIL;
            }
        } else {
            evbuf = ble_transport_alloc_evt(0);
            if (!evbuf) {
                ESP_LOGE(TAG, "Rx: transport_alloc_evt(0) failed");
                atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
                return ESP_FAIL;
            }
        }

        memset(evbuf, 0, sizeof *evbuf);
        memcpy(evbuf, &data[1], totlen);
        rc = ble_transport_to_hs_evt(evbuf);
        if (rc) {
            ESP_LOGE(TAG, "Rx: transport_to_hs_evt failed");
            atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
            return ESP_FAIL;
        }
    } else if (data[0] == HCI_H4_ACL) {
        struct os_mbuf *m = ble_transport_alloc_acl_from_ll();
        if (!m) {
            ESP_LOGE(TAG, "Rx: alloc_acl_from_ll failed");
            atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
            return ESP_FAIL;
        }
        if ((rc = os_mbuf_append(m, &data[1], len_total_read - 1)) != 0) {
            ESP_LOGE(TAG, "Rx: os_mbuf_append failed; rc=%d", rc);
            os_mbuf_free_chain(m);
            atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
            return ESP_FAIL;
        }
        ble_transport_to_hs_acl(m);
    }

    atomic_fetch_sub_explicit(&s_hci_refcount, 1, memory_order_release);
    return ESP_OK;
}

#endif // CONFIG_BT_NIMBLE_ENABLED && CONFIG_ESP_HOSTED_ENABLED
