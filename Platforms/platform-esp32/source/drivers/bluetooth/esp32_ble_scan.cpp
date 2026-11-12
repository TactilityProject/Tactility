#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>

#include <host/ble_gap.h>
#include <host/ble_hs_mbuf.h>

#include <algorithm>
#include <cstring>

constexpr auto* TAG = "esp32_ble_scan";
#include <tactility/log.h>

// ---- Module-static scan context ----
// Set at the start of ble_resolve_next_unnamed_peer; valid for the duration of
// the sequential resolution chain (single-device, single-scan at a time).
// Using BleCtx* (not Device*) so we avoid keeping a static Device reference.
static BleCtx* s_scan_ctx = nullptr;

// ---- Scan data helpers ----

/** Clears the scan-active flag and notifies subscribers, once. */
static void finish_scan_events(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    // Serialize with name_read_callback() so the finish event cannot overtake a peer result.
    xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
    if (!ble_get_scan_active(device)) {
        xSemaphoreGive(ctx->scan_mutex);
        return;
    }
    ble_set_scan_active(device, false);
    struct BtEvent e = {};
    e.type = BT_EVENT_SCAN_FINISHED;
    ble_publish_event(device, e);
    xSemaphoreGive(ctx->scan_mutex);
}

void ble_scan_abort(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    // Blocks the DISC_COMPLETE handler and the resolution chain from starting a replacement
    // connection after the cancels below.
    ctx->name_res_abort.store(true);
    uint16_t name_res_link = ctx->name_res_conn_handle.load();
    if (name_res_link != BLE_HS_CONN_HANDLE_NONE) {
        // An open name-read link blocks a GATT change and the bond swap, so end it here. The
        // disconnect handler clears name_res_conn_handle.
        ble_gap_terminate(name_res_link, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
    }
    if (ble_gap_conn_active()) {
        // The cancel completes on the host task, ble_prepare_profile_start() waits for it.
        ble_gap_conn_cancel();
    }
    finish_scan_events(device);
}

void ble_scan_clear_results(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
    ctx->scan_count = 0;
    if (ctx->scan_results != nullptr) {
        memset(ctx->scan_results, 0, BLE_SCAN_RESULTS_MAX * sizeof(BtPeerRecord));
    }
    xSemaphoreGive(ctx->scan_mutex);
}

// ---- GAP scan callback ----

int ble_gap_disc_event_handler(struct ble_gap_event* event, void* arg) {
    struct Device* device = (struct Device*)arg;
    BleCtx* ctx = ble_get_ctx(device);

    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            const auto& disc = event->disc;

            BtPeerRecord record = {};
            memcpy(record.addr, disc.addr.val, BT_ADDR_LEN);
            record.addr_type = disc.addr.type;
            record.rssi      = disc.rssi;
            record.paired    = false;
            record.connected = false;

            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) == 0) {
                if (fields.name != nullptr && fields.name_len > 0) {
                    size_t copy_len = std::min<size_t>(fields.name_len, BT_NAME_MAX);
                    memcpy(record.name, fields.name, copy_len);
                    record.name[copy_len] = '\0';
                }
            }

            {
                xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
                bool found = false;
                for (size_t i = 0; i < ctx->scan_count; ++i) {
                    if (memcmp(ctx->scan_results[i].addr, record.addr, BT_ADDR_LEN) == 0) {
                        // Deduplicate: merge name from SCAN_RSP without clobbering ADV_IND name
                        if (record.name[0] != '\0') {
                            memcpy(ctx->scan_results[i].name, record.name, BT_NAME_MAX + 1);
                        }
                        ctx->scan_results[i].rssi = record.rssi;
                        found = true;
                        break;
                    }
                }
                if (!found && ctx->scan_count < BLE_SCAN_RESULTS_MAX) {
                    ctx->scan_results[ctx->scan_count] = record;
                    ctx->scan_addrs[ctx->scan_count]   = disc.addr; // full addr (type+val)
                    ctx->scan_count++;
                }
                xSemaphoreGive(ctx->scan_mutex);
            }

            struct BtEvent e = {};
            e.type = BT_EVENT_PEER_FOUND;
            e.peer  = record;
            ble_publish_event(device, e);
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            LOG_I(TAG, "Scan complete (reason=%d)", event->disc_complete.reason);
            // scan_active stays true until name resolution finishes, so the UI spinner keeps
            // running. A scan aborted for a profile start skips resolution.
            if (ctx->name_res_abort.load()) {
                finish_scan_events(device);
            } else {
                ble_resolve_next_unnamed_peer(device, 0);
            }
            break;

        default:
            break;
    }
    return 0;
}

// ---- GATT Device Name resolution ----
//
// After a scan completes, briefly connect to each device that didn't include its
// name in advertising data and read Generic Access Device Name (UUID 0x2A00).
//
// Resolution is sequential: connect → read → disconnect → next device.
// Skip resolution if a profile server or HID host connection is active —
// a simultaneous central connection would fail with BLE_HS_EALREADY.

static int name_read_callback(uint16_t conn_handle, const struct ble_gatt_error* error,
                              struct ble_gatt_attr* attr, void* arg) {
    BleCtx* ctx = s_scan_ctx;

    if (error->status == 0 && attr != nullptr) {
        size_t idx = (size_t)(uintptr_t)arg;
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        if (len > 0 && len <= (uint16_t)BT_NAME_MAX) {
            char name_buf[BT_NAME_MAX + 1] = {};
            os_mbuf_copydata(attr->om, 0, len, name_buf);
            {
                // Held across the publication so a concurrent ble_scan_abort() cannot publish
                // SCAN_FINISHED between the name update and this peer event.
                xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
                if (idx < ctx->scan_count && ctx->scan_results[idx].name[0] == '\0') {
                    memcpy(ctx->scan_results[idx].name, name_buf, len);
                    ctx->scan_results[idx].name[len] = '\0';
                    LOG_I(TAG, "Name resolved (idx=%u): %s", (unsigned)idx, name_buf);
                }
                BtPeerRecord record = (idx < ctx->scan_count) ? ctx->scan_results[idx] : BtPeerRecord{};
                if (!ctx->name_res_abort.load()) {
                    struct BtEvent e = {};
                    e.type = BT_EVENT_PEER_FOUND;
                    e.peer  = record;
                    ble_publish_event(ctx->device, e);
                }
                xSemaphoreGive(ctx->scan_mutex);
            }
        }
        return 0; // wait for BLE_HS_EDONE
    }

    // BLE_HS_EDONE, ATT error, or timeout — done with this device
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int name_res_gap_callback(struct ble_gap_event* event, void* arg) {
    size_t idx = (size_t)(uintptr_t)arg;
    BleCtx* ctx = s_scan_ctx;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ctx->name_res_conn_handle.store(event->connect.conn_handle);
                LOG_I(TAG, "Name resolution: connected (idx=%u handle=%u)", (unsigned)idx, event->connect.conn_handle);
                static const ble_uuid16_t device_name_uuid = BLE_UUID16_INIT(0x2A00);
                int rc = ble_gattc_read_by_uuid(event->connect.conn_handle,
                                                1, 0xFFFF,
                                                &device_name_uuid.u,
                                                name_read_callback, arg);
                if (rc != 0) {
                    LOG_W(TAG, "Name resolution: read_by_uuid failed rc=%d", rc);
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                }
            } else {
                LOG_I(TAG, "Name resolution: connect failed (idx=%u status=%d)", (unsigned)idx, event->connect.status);
                ble_resolve_next_unnamed_peer(ctx->device, idx + 1);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ctx->name_res_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
            LOG_I(TAG, "Name resolution: disconnected (idx=%u)", (unsigned)idx);
            ble_resolve_next_unnamed_peer(ctx->device, idx + 1);
            break;

        default:
            break;
    }
    return 0;
}

void ble_resolve_next_unnamed_peer(struct Device* device, size_t start_idx) {
    BleCtx* ctx = ble_get_ctx(device);
    s_scan_ctx = ctx;

    // A profile start aborted the scan, so do not chain into another connection.
    if (ctx->name_res_abort.load()) {
        LOG_I(TAG, "Name resolution: aborted (profile start)");
        finish_scan_events(device);
        return;
    }

    // A simultaneous central connection would fail while a profile or HID host link is active.
    if (ble_midi_get_active(device) || ble_spp_get_active(device) ||
        ble_hid_get_active(device)  || ble_hid_get_host_active(device)) {
        LOG_I(TAG, "Name resolution: skipping (server or HID host active)");
        finish_scan_events(device);
        return;
    }

    size_t i = start_idx;
    while (true) {
        ble_addr_t addr     = {};
        bool       found    = false;
        bool       radio_on = false;
        int        rc       = -1;

        // Re-checked every iteration, the chain continues from GAP callbacks on the host task.
        if (ctx->name_res_abort.load()) {
            LOG_I(TAG, "Name resolution: aborted (profile start)");
            finish_scan_events(device);
            return;
        }

        // Never start a connection while the radio is going down, nimble_port_stop() would
        // hang. Checked per iteration and under scan_mutex, together with the connect call.
        xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
        radio_on = ctx->radio_state.load() == BT_RADIO_STATE_ON;
        if (radio_on) {
            while (i < ctx->scan_count) {
                if (ctx->scan_results[i].name[0] == '\0') {
                    addr  = ctx->scan_addrs[i];
                    found = true;
                    break;
                }
                ++i;
            }
            if (found) {
                // Central role always uses the public address, so bonds with peers we connect
                // to survive identity changes.
                void* idx_arg = (void*)(uintptr_t)i;
                rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 1500, nullptr,
                                     name_res_gap_callback, idx_arg);
            }
        }
        xSemaphoreGive(ctx->scan_mutex);

        if (!radio_on) {
            LOG_I(TAG, "Name resolution: aborting (radio not on)");
            ble_set_scan_active(device, false);
            struct BtEvent e = {};
            e.type = BT_EVENT_SCAN_FINISHED;
            ble_publish_event(device, e);
            return;
        }

        if (!found) {
            LOG_I(TAG, "Name resolution: complete (%u devices)", (unsigned)i);
            ble_set_scan_active(device, false);
            struct BtEvent e = {};
            e.type = BT_EVENT_SCAN_FINISHED;
            ble_publish_event(device, e);
            return;
        }

        if (rc == 0) {
            return; // name_res_gap_callback continues the chain
        }

        LOG_I(TAG, "Name resolution: ble_gap_connect failed idx=%u rc=%d, skipping", (unsigned)i, rc);
        ++i;
    }
}

#endif // CONFIG_BT_NIMBLE_ENABLED
