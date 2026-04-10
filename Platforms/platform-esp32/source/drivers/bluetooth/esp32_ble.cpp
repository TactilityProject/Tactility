#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>

#include <tactility/device.h>
#include <tactility/driver.h>

#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <store/config/ble_store_config.h>

#include <atomic>
#include <cstring>

constexpr auto* TAG = "esp32_ble";
#include <tactility/log.h>
#include <esp_timer.h>

// ble_store_config_init() is not declared in the public header in some IDF versions.
extern "C" void ble_store_config_init(void);

// Sub-module API structs (defined in their respective cpp files).
extern const BtHidDeviceApi nimble_hid_device_api;
extern const BtSerialApi nimble_serial_api;
extern const BtMidiApi   nimble_midi_api;

// File-static BleCtx pointer used only by NimBLE host callbacks whose signature
// is fixed by the NimBLE API (on_sync, on_reset) and cannot carry a Device*.
// All other callbacks receive Device* via their void* arg parameter.
static BleCtx* s_ctx = nullptr;

// ---- Context accessor ----
// Returns the BleCtx for any BLE device (root or child).
// If device is the root BLE device (BLUETOOTH_TYPE) its driver_data IS the BleCtx.
// If device is a child BLE device its parent is the root BLE device.
// We must NOT use device_get_parent blindly: the DTS root node is the parent of ble0,
// so device_get_parent(ble0) returns a non-null node whose driver_data is not BleCtx.

BleCtx* ble_get_ctx(struct Device* device) {
    if (device_get_type(device) == &BLUETOOTH_TYPE) {
        return (BleCtx*)device_get_driver_data(device);
    }
    struct Device* parent = device_get_parent(device);
    return parent ? (BleCtx*)device_get_driver_data(parent) : nullptr;
}

// ---- Forward declarations ----
static void host_task(void* param);
static void on_sync();
static void on_reset(int reason);
static void dispatch_enable(BleCtx* ctx);
static void dispatch_disable(BleCtx* ctx);
static int  gap_event_handler(struct ble_gap_event* event, void* arg);

// ---- General field accessor implementations ----

BtRadioState ble_get_radio_state(struct Device* device) {
    return ble_get_ctx(device)->radio_state.load();
}

bool ble_hid_get_host_active(struct Device* device) {
    return ble_get_ctx(device)->hid_host_active.load();
}
bool ble_get_scan_active(struct Device* device) {
    return ble_get_ctx(device)->scan_active.load();
}
void ble_set_scan_active(struct Device* device, bool v) {
    ble_get_ctx(device)->scan_active.store(v);
}

// ---- Event publishing ----

void ble_publish_event(struct Device* device, struct BtEvent event) {
    BleCtx* ctx = ble_get_ctx(device);
    if (!ctx) return;
    // Copy under mutex so callbacks can safely call add/remove_event_callback
    BleCallbackEntry local[BLE_MAX_CALLBACKS];
    size_t count;
    xSemaphoreTake(ctx->cb_mutex, portMAX_DELAY);
    count = ctx->callback_count;
    memcpy(local, ctx->callbacks, count * sizeof(BleCallbackEntry));
    xSemaphoreGive(ctx->cb_mutex);
    for (size_t i = 0; i < count; i++) {
        local[i].fn(ctx->device, local[i].ctx, event);
    }
}

// ---- Advertising restart helper ----

static void adv_restart_callback(void* arg) {
    struct Device* device = (struct Device*)arg;
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx->radio_state.load() != BT_RADIO_STATE_ON) return;

    uint16_t hid_conn = ble_hid_get_conn_handle(ctx->hid_device_child);

    if (ctx->midi_active.load() && ctx->midi_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
        ble_start_advertising(device, &MIDI_SVC_UUID);
    } else if (ctx->spp_active.load() && ctx->spp_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
        ble_start_advertising(device, &NUS_SVC_UUID);
    } else if (ctx->hid_active.load() && hid_conn == BLE_HS_CONN_HANDLE_NONE) {
        ble_start_advertising_hid(device, hid_appearance);
    }
}

void ble_schedule_adv_restart(struct Device* device, uint64_t delay_us) {
    BleCtx* ctx = ble_get_ctx(device);
    if (delay_us == 0) {
        adv_restart_callback(device);
        return;
    }
    if (ctx->adv_restart_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback        = adv_restart_callback;
        args.arg             = device;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name            = "ble_adv_restart";
        int rc = esp_timer_create(&args, &ctx->adv_restart_timer);
        if (rc != ESP_OK) {
            LOG_E(TAG, "adv_restart timer create failed (rc=%d)", rc);
            return;
        }
    }
    esp_timer_stop(ctx->adv_restart_timer);
    int rc = esp_timer_start_once(ctx->adv_restart_timer, delay_us);
    if (rc != ESP_OK) {
        LOG_E(TAG, "adv_restart timer start failed (rc=%d)", rc);
    }
}

// ---- GAP connection event handler ----

static int gap_event_handler(struct ble_gap_event* event, void* arg) {
    struct Device* device = (struct Device*)arg;
    BleCtx* ctx = ble_get_ctx(device);

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                LOG_I(TAG, "Connected (handle=%u hid_active=%d hid_conn=%u)",
                         event->connect.conn_handle,
                         (int)ctx->hid_active.load(),
                         (unsigned)ble_hid_get_conn_handle(ctx->hid_device_child));
                // Do NOT call ble_gap_security_initiate() here.
                // Windows BLE MIDI initiates encryption itself; calling here creates a race
                // with REPEAT_PAIRING+RETRY → two concurrent SM procedures → disconnect.
                // On esp_hosted, ENC_CHANGE can arrive BEFORE CONNECT, so re-initiating
                // on an already-encrypted link triggers unwanted re-pairing.
            } else {
                LOG_W(TAG, "Connection failed (status=%d)", event->connect.status);
                // Delay restart so NimBLE can clean up SMP/connection state before peer retries.
                ble_schedule_adv_restart(device, 500'000);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            LOG_I(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
            uint16_t hdl = event->disconnect.conn.conn_handle;
            bool was_spp  = (ctx->spp_conn_handle.load()  == hdl);
            bool was_midi = (ctx->midi_conn_handle.load() == hdl);
            bool was_hid  = (ble_hid_get_conn_handle(ctx->hid_device_child) == hdl);
            if (was_spp)  ctx->spp_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
            if (was_midi) { ctx->midi_conn_handle.store(BLE_HS_CONN_HANDLE_NONE); ctx->midi_use_indicate.store(false); }
            if (was_hid)  ble_hid_set_conn_handle(ctx->hid_device_child, BLE_HS_CONN_HANDLE_NONE);
            ctx->link_encrypted.store(false);
            // If HID was stopped while connected, switch profile to None now that the
            // connection is gone. ble_gatts_mutable() is true here (no active connection,
            // advertising stopped by hid_device_stop), so switch_profile is safe.
            // Skip during shutdown — ble_gatts_reset() is unsafe while nimble_port_stop() runs.
            if (was_hid && !ctx->hid_active.load() && current_hid_profile != BleHidProfile::None &&
                ctx->radio_state.load() != BT_RADIO_STATE_OFF_PENDING) {
                ble_hid_switch_profile(ctx->hid_device_child, BleHidProfile::None);
            }
            // Restart advertising whenever a service is active without a live connection.
            // Covers both normal disconnect and Windows discovery-only connections.
            // Skip if the radio is going OFF_PENDING — nimble_port_stop() is in progress
            // and calling ble_gap_adv_start() from within the NimBLE host task while the
            // controller is shutting down would block the host task and hang nimble_port_stop().
            if (ctx->radio_state.load() != BT_RADIO_STATE_OFF_PENDING) {
                uint16_t hid_conn_now = ble_hid_get_conn_handle(ctx->hid_device_child);
                if (ctx->midi_active.load() && ctx->midi_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
                    ble_start_advertising(device, &MIDI_SVC_UUID);
                } else if (ctx->spp_active.load() && ctx->spp_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
                    ble_start_advertising(device, &NUS_SVC_UUID);
                } else if (ctx->hid_active.load() && hid_conn_now == BLE_HS_CONN_HANDLE_NONE) {
                    ble_start_advertising_hid(device, hid_appearance);
                }
            }
            break;
        }

        case BLE_GAP_EVENT_SUBSCRIBE:
            LOG_I(TAG, "Subscribe attr=%u cur_notify=%u cur_indicate=%u (nus_tx=%u midi_io=%u)",
                     event->subscribe.attr_handle,
                     (unsigned)event->subscribe.cur_notify,
                     (unsigned)event->subscribe.cur_indicate,
                     nus_tx_handle, midi_io_handle);
            if (event->subscribe.attr_handle != nus_tx_handle &&
                event->subscribe.attr_handle != midi_io_handle &&
                event->subscribe.cur_indicate) {
                // Service Changed subscription — ignore (Windows discovers on its own)
                LOG_I(TAG, "Service Changed subscription (attr=%u) — ignoring",
                         event->subscribe.attr_handle);
            } else if (event->subscribe.attr_handle == nus_tx_handle) {
                if (!ctx->spp_active.load()) {
                    LOG_I(TAG, "SPP CCCD subscribed but spp_active=false — ignoring");
                    break;
                }
                ctx->spp_conn_handle.store(event->subscribe.conn_handle);
                LOG_I(TAG, "SPP client subscribed (nus_tx=%u)", nus_tx_handle);
                // Fire PROFILE_STATE_CHANGED so Tactility bridge can persist the profile
                // off the NimBLE host task (file I/O → stack overflow on nimble_host).
                {
                    struct ble_gap_conn_desc sub_desc = {};
                    if (ble_gap_conn_find(event->subscribe.conn_handle, &sub_desc) == 0) {
                        struct BtEvent e = {};
                        e.type = BT_EVENT_PROFILE_STATE_CHANGED;
                        memcpy(e.profile_state.addr, sub_desc.peer_id_addr.val, 6);
                        e.profile_state.profile = BT_PROFILE_SPP;
                        e.profile_state.state   = BT_PROFILE_STATE_CONNECTED;
                        ble_publish_event(device, e);
                    }
                }
            } else if (event->subscribe.attr_handle == midi_io_handle) {
                if ((event->subscribe.cur_notify || event->subscribe.cur_indicate) && !ctx->midi_active.load()) {
                    LOG_I(TAG, "MIDI CCCD subscribed but midi_active=false — ignoring");
                    break;
                }
                if (event->subscribe.cur_notify || event->subscribe.cur_indicate) {
                    ctx->midi_conn_handle.store(event->subscribe.conn_handle);
                    ctx->midi_use_indicate.store(event->subscribe.cur_indicate != 0);
                    LOG_I(TAG, "MIDI client subscribed (midi_io=%u indicate=%d)",
                             midi_io_handle, (int)ctx->midi_use_indicate.load());
                    // Fire PROFILE_STATE_CHANGED so Tactility bridge persists the profile.
                    {
                        struct ble_gap_conn_desc sub_desc = {};
                        if (ble_gap_conn_find(event->subscribe.conn_handle, &sub_desc) == 0) {
                            struct BtEvent e = {};
                            e.type = BT_EVENT_PROFILE_STATE_CHANGED;
                            memcpy(e.profile_state.addr, sub_desc.peer_id_addr.val, 6);
                            e.profile_state.profile = BT_PROFILE_MIDI;
                            e.profile_state.state   = BT_PROFILE_STATE_CONNECTED;
                            ble_publish_event(device, e);
                        }
                    }
                    // Send MIDI Active Sensing immediately after subscription.
                    static const uint8_t active_sensing_pkt[3] = { 0x80, 0x80, 0xFE };
                    struct os_mbuf* as_om = ble_hs_mbuf_from_flat(active_sensing_pkt, 3);
                    if (as_om != nullptr) {
                        int as_rc = ctx->midi_use_indicate.load()
                            ? ble_gatts_indicate_custom(ctx->midi_conn_handle.load(), midi_io_handle, as_om)
                            : ble_gatts_notify_custom(ctx->midi_conn_handle.load(), midi_io_handle, as_om);
                        if (as_rc != 0) os_mbuf_free_chain(as_om);
                        LOG_I(TAG, "Active Sensing (subscribe) rc=%d", as_rc);
                    }
                } else {
                    ctx->midi_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
                    ctx->midi_use_indicate.store(false);
                    LOG_I(TAG, "MIDI client unsubscribed");
                }
            } else if (event->subscribe.cur_notify &&
                       (event->subscribe.attr_handle == hid_kb_input_handle ||
                        event->subscribe.attr_handle == hid_consumer_input_handle ||
                        event->subscribe.attr_handle == hid_mouse_input_handle ||
                        event->subscribe.attr_handle == hid_gamepad_input_handle)) {
                const char* rpt_name =
                    (event->subscribe.attr_handle == hid_kb_input_handle)       ? "keyboard" :
                    (event->subscribe.attr_handle == hid_consumer_input_handle)  ? "consumer" :
                    (event->subscribe.attr_handle == hid_mouse_input_handle)     ? "mouse"    :
                    (event->subscribe.attr_handle == hid_gamepad_input_handle)   ? "gamepad"  : "unknown";
                if (!ctx->hid_active.load()) {
                    LOG_I(TAG, "HID CCCD subscribed (%s) but hid_active=false — ignoring", rpt_name);
                    break;
                }
                LOG_I(TAG, "HID CCCD subscribed: %s (attr=%u conn=%u)",
                         rpt_name, event->subscribe.attr_handle, event->subscribe.conn_handle);
                if (ble_hid_get_conn_handle(ctx->hid_device_child) == BLE_HS_CONN_HANDLE_NONE) {
                    ble_hid_set_conn_handle(ctx->hid_device_child, event->subscribe.conn_handle);
                }
            }
            break;

        case BLE_GAP_EVENT_MTU:
            LOG_I(TAG, "MTU updated (conn=%u mtu=%u)",
                     event->mtu.conn_handle, event->mtu.value);
            break;

        case BLE_GAP_EVENT_CONN_UPDATE: {
            struct ble_gap_conn_desc desc = {};
            if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
                LOG_I(TAG, "Conn params updated (status=%d itvl=%u latency=%u timeout=%u)",
                         event->conn_update.status, desc.conn_itvl,
                         desc.conn_latency, desc.supervision_timeout);
            }
            break;
        }

        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            *event->conn_update_req.self_params = *event->conn_update_req.peer_params;
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            LOG_I(TAG, "Encryption changed (conn=%u status=%d)",
                     event->enc_change.conn_handle, event->enc_change.status);
            if (event->enc_change.status == 0) {
                ctx->link_encrypted.store(true);
                // For HID device: set hid_conn_handle at encryption time so Phase 2 bonded
                // reconnects are tracked even when BLE_GAP_EVENT_SUBSCRIBE doesn't fire.
                // Windows only writes HID CCCDs in Phase 2; NimBLE may restore them from NVS
                // silently (no SUBSCRIBE event). Without this, hid_conn_handle stays NONE
                // and hid_device_is_connected() returns false for the entire Phase 2 session.
                if (ctx->hid_active.load() &&
                    ble_hid_get_conn_handle(ctx->hid_device_child) == BLE_HS_CONN_HANDLE_NONE) {
                    ble_hid_set_conn_handle(ctx->hid_device_child, event->enc_change.conn_handle);
                    LOG_I(TAG, "HID conn handle set on ENC_CHANGE (conn=%u)",
                             event->enc_change.conn_handle);
                }
                // Fire PAIR_RESULT so Tactility bridge can persist the paired device
                // off the NimBLE host task (file I/O → stack overflow on nimble_host).
                struct ble_gap_conn_desc desc = {};
                if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                    struct BtEvent e = {};
                    e.type = BT_EVENT_PAIR_RESULT;
                    memcpy(e.pair_result.addr, desc.peer_id_addr.val, 6);
                    e.pair_result.result  = BT_PAIR_RESULT_SUCCESS;
                    e.pair_result.profile = ctx->midi_active.load() ? BT_PROFILE_MIDI
                                          : ctx->spp_active.load()  ? BT_PROFILE_SPP
                                          : ctx->hid_active.load()  ? BT_PROFILE_HID_DEVICE
                                                                     : BT_PROFILE_HID_HOST;
                    ble_publish_event(device, e);
                }
            }
            // Re-send Active Sensing now that the link is encrypted.
            if (event->enc_change.status == 0 &&
                ctx->midi_conn_handle.load() == event->enc_change.conn_handle) {
                static const uint8_t as_pkt[3] = { 0x80, 0x80, 0xFE };
                struct os_mbuf* om = ble_hs_mbuf_from_flat(as_pkt, 3);
                if (om != nullptr) {
                    int rc = ctx->midi_use_indicate.load()
                        ? ble_gatts_indicate_custom(ctx->midi_conn_handle.load(), midi_io_handle, om)
                        : ble_gatts_notify_custom(ctx->midi_conn_handle.load(), midi_io_handle, om);
                    if (rc != 0) os_mbuf_free_chain(om);
                    LOG_I(TAG, "Active Sensing (post-enc) rc=%d", rc);
                }
            }
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            LOG_I(TAG, "Passkey action (conn=%u action=%u)",
                     event->passkey.conn_handle, event->passkey.params.action);
            struct ble_sm_io pkey = {};
            pkey.action = event->passkey.params.action;
            if (pkey.action == BLE_SM_IOACT_NONE) {
                ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            }
            break;
        }

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            LOG_I(TAG, "Repeat pairing (conn=%u encrypted=%d)",
                     event->repeat_pairing.conn_handle, (int)ctx->link_encrypted.load());
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
                if (!ctx->link_encrypted.load()) {
                    // Fire BOND_LOST so Tactility bridge removes the stored device record.
                    struct BtEvent e = {};
                    e.type = BT_EVENT_PAIR_RESULT;
                    memcpy(e.pair_result.addr, desc.peer_id_addr.val, 6);
                    e.pair_result.result = BT_PAIR_RESULT_BOND_LOST;
                    ble_publish_event(device, e);
                }
            }
            // If already encrypted, IGNORE so the current session continues.
            // RETRY would start a 30-second SMP timer that always expires (peer won't
            // respond to a new Pair Request on an encrypted link) → forced disconnect.
            if (ctx->link_encrypted.load()) {
                LOG_I(TAG, "Repeat pairing: link encrypted — ignoring");
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        default:
            break;
    }
    return 0;
}

// ---- NimBLE host sync / reset callbacks ----

static void on_sync() {
    LOG_I(TAG, "NimBLE synced (nus_tx=%u midi_io=%u hid_kb=%u hid_cs=%u hid_ms=%u hid_gp=%u)",
             nus_tx_handle, midi_io_handle,
             hid_kb_input_handle, hid_consumer_input_handle,
             hid_mouse_input_handle, hid_gamepad_input_handle);
    BleCtx* ctx = s_ctx;
    if (ctx == nullptr) return;

    ctx->pending_reset_count.store(0);

    uint8_t own_addr_type;
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) LOG_E(TAG, "ensure_addr failed (rc=%d)", rc);
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) LOG_E(TAG, "infer addr type failed (rc=%d)", rc);

    // Sync GATT handle values — pass the child device so the GATT callbacks
    // can retrieve child driver data (BleSppCtx / BleMidiCtx) without globals.
    ble_spp_init_gatt_handles(ctx->serial_child);
    ble_midi_init_gatt_handles(ctx->midi_child);

    ctx->radio_state.store(BT_RADIO_STATE_ON);
    struct BtEvent e = {};
    e.type = BT_EVENT_RADIO_STATE_CHANGED;
    e.radio_state = BT_RADIO_STATE_ON;
    ble_publish_event(ctx->device, e);

    // The Tactility bridge handles auto-start (SPP/MIDI/HID) in response to
    // BT_EVENT_RADIO_STATE_CHANGED(ON), dispatched to the main task above.
    // On a multi-core ESP32 the main task can preempt the NimBLE host task
    // between ble_publish_event() and here, call hid/spp/midi_device_start(),
    // set hid/spp/midi_active, and already start profile-specific advertising.
    // Only start name-only advertising if no profile has been activated yet;
    // otherwise we would overwrite the correct profile advertising with name-only,
    // causing Windows to connect without seeing the HID/SPP/MIDI service UUID.
    if (!ctx->hid_active.load() && !ctx->spp_active.load() && !ctx->midi_active.load()) {
        ble_start_advertising(ctx->device, nullptr);
    }
}

static void dispatch_disable_timer_cb(void* arg) {
    BleCtx* ctx = (BleCtx*)arg;
    dispatch_disable(ctx);
}

static void on_reset(int reason) {
    LOG_W(TAG, "NimBLE host reset (reason=%d)", reason);
    BleCtx* ctx = s_ctx;
    if (ctx == nullptr) return;

    if (ctx->radio_state.load() == BT_RADIO_STATE_ON_PENDING) {
        int count = ctx->pending_reset_count.fetch_add(1) + 1;
        if (count == 3) {
            LOG_E(TAG, "BT controller unresponsive after 3 resets — giving up");
            // Can't call nimble_port_stop() from the NimBLE host task.
            // Fire a one-shot esp_timer (delay=0 → fires on esp_timer task immediately).
            if (ctx->disable_timer != nullptr) {
                esp_timer_start_once(ctx->disable_timer, 0);
            }
        }
    }
}

static void host_task(void* param) {
    LOG_I(TAG, "BLE host task started");
    nimble_port_run();
    // nimble_port_deinit() is called by dispatch_disable() after nimble_port_stop() returns.
    nimble_port_freertos_deinit();
}

// ---- Advertising helpers ----

void ble_start_advertising(struct Device* device, const ble_uuid128_t* svc_uuid) {
    ble_gap_adv_stop();

    // Always sync the GAP name from ctx right before building the advertising packet,
    // so bluetooth_set_device_name() is honoured regardless of call timing.
    BleCtx* _name_ctx = ble_get_ctx(device);
    if (_name_ctx) ble_svc_gap_device_name_set(_name_ctx->device_name);

    int rc;
    if (svc_uuid != nullptr) {
        const char* name = ble_svc_gap_device_name();
        uint8_t name_len  = (uint8_t)strlen(name);
        uint8_t short_len = (name_len > 8) ? 8 : name_len;

        ble_uuid128_t uuid_copy = *svc_uuid;
        struct ble_hs_adv_fields fields;
        memset(&fields, 0, sizeof(fields));
        fields.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.uuids128             = &uuid_copy;
        fields.num_uuids128         = 1;
        fields.uuids128_is_complete = 1;
        fields.name                 = (const uint8_t*)name;
        fields.name_len             = short_len;
        fields.name_is_complete     = 0; // AD type 0x08 = Shortened Local Name

        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            LOG_W(TAG, "startAdvertising: set_fields with name failed rc=%d, retrying", rc);
            fields.name     = nullptr;
            fields.name_len = 0;
            rc = ble_gap_adv_set_fields(&fields);
            if (rc != 0) {
                LOG_E(TAG, "startAdvertising: set_fields failed rc=%d", rc);
                return;
            }
        }
        struct ble_hs_adv_fields rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.name             = (const uint8_t*)name;
        rsp.name_len         = name_len;
        rsp.name_is_complete = 1;
        rc = ble_gap_adv_rsp_set_fields(&rsp);
        if (rc != 0) {
            LOG_W(TAG, "startAdvertising: rsp_set_fields rc=%d (non-fatal)", rc);
        }
        LOG_I(TAG, "startAdvertising: UUID mode (uuid[0..3]=%02x%02x%02x%02x)",
                 svc_uuid->value[0], svc_uuid->value[1],
                 svc_uuid->value[2], svc_uuid->value[3]);
    } else {
        struct ble_hs_adv_fields fields;
        memset(&fields, 0, sizeof(fields));
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        const char* name = ble_svc_gap_device_name();
        fields.name             = (const uint8_t*)name;
        fields.name_len         = (uint8_t)strlen(name);
        fields.name_is_complete = 1;
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            LOG_E(TAG, "startAdvertising: set_fields failed rc=%d", rc);
            return;
        }
        LOG_I(TAG, "startAdvertising: name-only mode");
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 160; // 100 ms
    adv_params.itvl_max  = 240; // 150 ms

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, device);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        LOG_E(TAG, "startAdvertising: adv_start failed rc=%d", rc);
    } else {
        LOG_I(TAG, "startAdvertising: OK");
    }
}

void ble_start_advertising_hid(struct Device* device, uint16_t appearance) {
    ble_gap_adv_stop();

    // Always sync the GAP name from ctx right before building the advertising packet.
    BleCtx* _name_ctx = ble_get_ctx(device);
    if (_name_ctx) ble_svc_gap_device_name_set(_name_ctx->device_name);

    const char* name  = ble_svc_gap_device_name();
    uint8_t name_len  = (uint8_t)strlen(name);
    uint8_t short_len = (name_len > 8) ? 8 : name_len;

    static const ble_uuid16_t hid_uuid16 = BLE_UUID16_INIT(0x1812);

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance           = appearance;
    fields.appearance_is_present = 1;
    fields.uuids16              = &hid_uuid16;
    fields.num_uuids16          = 1;
    fields.uuids16_is_complete  = 1;
    fields.name                 = (const uint8_t*)name;
    fields.name_len             = short_len;
    fields.name_is_complete     = 0;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        LOG_W(TAG, "startAdvertisingHid: set_fields with name failed rc=%d, retrying", rc);
        fields.name     = nullptr;
        fields.name_len = 0;
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            LOG_E(TAG, "startAdvertisingHid: set_fields failed rc=%d", rc);
            return;
        }
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.name             = (const uint8_t*)name;
    rsp.name_len         = name_len;
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        LOG_W(TAG, "startAdvertisingHid: rsp_set_fields rc=%d (non-fatal)", rc);
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 160;
    adv_params.itvl_max  = 240;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, device);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        LOG_E(TAG, "startAdvertisingHid: adv_start failed rc=%d", rc);
    } else {
        LOG_I(TAG, "startAdvertisingHid: OK");
    }
}

// ---- Dispatch helpers ----

static void dispatch_enable(BleCtx* ctx) {
    LOG_I(TAG, "dispatch_enable()");

    if (ctx->radio_state.load() != BT_RADIO_STATE_OFF) {
        LOG_W(TAG, "Cannot enable from current state");
        return;
    }

    ctx->radio_state.store(BT_RADIO_STATE_ON_PENDING);
    {
        struct BtEvent e = {};
        e.type = BT_EVENT_RADIO_STATE_CHANGED;
        e.radio_state = BT_RADIO_STATE_ON_PENDING;
        ble_publish_event(ctx->device, e);
    }

    int rc = nimble_port_init();
    if (rc != 0) {
        LOG_E(TAG, "nimble_port_init failed (rc=%d)", rc);
        ctx->radio_state.store(BT_RADIO_STATE_OFF);
        struct BtEvent e = {};
        e.type = BT_EVENT_RADIO_STATE_CHANGED;
        e.radio_state = BT_RADIO_STATE_OFF;
        ble_publish_event(ctx->device, e);
        return;
    }

    ble_hs_cfg.sync_cb         = on_sync;
    ble_hs_cfg.reset_cb        = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 1;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();
    ble_hs_cfg.store_read_cb   = ble_store_config_read;
    ble_hs_cfg.store_write_cb  = ble_store_config_write;
    ble_hs_cfg.store_delete_cb = ble_store_config_delete;

    // Clear any stale "value_changed" flags left in NVS CCCD records.
    // Old firmware called ble_svc_gatt_changed(0, 0xFFFF) from switchGattProfile(),
    // which sets value_changed=1 in NVS for all bonded-but-disconnected peers.
    // On reconnect NimBLE sends a Service Changed indication; Windows disconnects
    // without ACKing (it re-discovers instead), so NimBLE never clears the flag,
    // causing an infinite reconnect loop. Purge all value_changed flags on every
    // enable so the first reconnect after re-enable always succeeds.
    {
        // Zero-initialize: peer_addr={0} matches any peer (== BLE_ADDR_ANY),
        // chr_val_handle=0 matches any handle — both are wildcards per the
        // ble_store_config_find_cccd() implementation.
        struct ble_store_key_cccd cccd_key = {};
        struct ble_store_value_cccd cccd_val = {};
        int n_cleared = 0;
        while (ble_store_read_cccd(&cccd_key, &cccd_val) == 0) {
            if (cccd_val.value_changed) {
                cccd_val.value_changed = 0;
                ble_store_write_cccd(&cccd_val);
                n_cleared++;
            }
            cccd_key.idx++;
        }
        if (n_cleared > 0) {
            LOG_I(TAG, "Cleared %d stale CCCD value_changed flag(s) from NVS", n_cleared);
        }
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register base GATT services (NUS + MIDI; HID added by switch_profile when started)
    ble_hid_init_gatt();

    ble_svc_gap_device_name_set(ctx->device_name);
    ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);

#if defined(CONFIG_ESP_HOSTED_ENABLED)
    ble_hci_gate_set_active(true);  // Open gate: NimBLE transport pool is ready
#endif
    // Start NimBLE host task (on_sync will fire when ready)
    nimble_port_freertos_init(host_task);
}

static void dispatch_disable(BleCtx* ctx) {
    LOG_I(TAG, "dispatch_disable()");

    if (ctx->radio_state.load() == BT_RADIO_STATE_OFF) {
        LOG_W(TAG, "Already off");
        return;
    }

    ctx->radio_state.store(BT_RADIO_STATE_OFF_PENDING);
    {
        struct BtEvent e = {};
        e.type = BT_EVENT_RADIO_STATE_CHANGED;
        e.radio_state = BT_RADIO_STATE_OFF_PENDING;
        ble_publish_event(ctx->device, e);
    }

    // Blocking: waits for nimble_port_run() to exit.
    // Do NOT call ble_gap_adv_stop()/disc_cancel() before — if controller is
    // unresponsive they generate more HCI timeouts before the stop takes effect.
    // The HCI gate must stay OPEN here: nimble_port_stop() → ble_hs_stop() sends
    // HCI commands and needs to receive the command-complete events back from the
    // controller. Closing the gate before this point starves NimBLE of those events,
    // causing HCI timeouts and a cascade of resets that crash in ble_hs_timer_sched.
    nimble_port_stop();
#if defined(CONFIG_ESP_HOSTED_ENABLED)
    // Close the gate NOW — after nimble_port_stop() returns the NimBLE host task has
    // exited and nimble_port_deinit() is about to zero npl_funcs. Any HCI packet
    // arriving after this point must not reach ble_transport_alloc_evt().
    ble_hci_gate_set_active(false);
    if (!ble_hci_gate_wait_idle(20)) {
        LOG_W(TAG, "HCI gate drain timed out");
    }
#endif
    nimble_port_deinit();

    ctx->spp_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->spp_active.store(false);
    ctx->midi_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->midi_active.store(false);
    ctx->hid_active.store(false);
    ctx->link_encrypted.store(false);
    ctx->pending_reset_count.store(0);
    current_hid_profile    = BleHidProfile::None;
    active_hid_rpt_map     = nullptr;
    active_hid_rpt_map_len = 0;

    // Clear hid_conn_handle in the child device driver data if still alive.
    ble_hid_set_conn_handle(ctx->hid_device_child, BLE_HS_CONN_HANDLE_NONE);

    if (ctx->midi_keepalive_timer != nullptr) {
        esp_timer_stop(ctx->midi_keepalive_timer);
        esp_timer_delete(ctx->midi_keepalive_timer);
        ctx->midi_keepalive_timer = nullptr;
    }
    if (ctx->adv_restart_timer != nullptr) {
        esp_timer_stop(ctx->adv_restart_timer);
        esp_timer_delete(ctx->adv_restart_timer);
        ctx->adv_restart_timer = nullptr;
    }

    ctx->radio_state.store(BT_RADIO_STATE_OFF);
    {
        struct BtEvent e = {};
        e.type = BT_EVENT_RADIO_STATE_CHANGED;
        e.radio_state = BT_RADIO_STATE_OFF;
        ble_publish_event(ctx->device, e);
    }
}

// ---- BluetoothApi implementations ----

static error_t api_get_radio_state(struct Device* device, enum BtRadioState* state) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !state) return ERROR_INVALID_ARGUMENT;
    *state = ctx->radio_state.load();
    return ERROR_NONE;
}

static error_t api_set_radio_enabled(struct Device* device, bool enabled) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_INVALID_STATE;
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    if (enabled) {
        dispatch_enable(ctx);
    } else {
        dispatch_disable(ctx);
    }
    xSemaphoreGive(ctx->radio_mutex);
    return ERROR_NONE;
}

static error_t api_scan_start(struct Device* device) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_INVALID_STATE;

    if (ctx->radio_state.load() != BT_RADIO_STATE_ON) {
        LOG_W(TAG, "scan_start: radio not on");
        return ERROR_INVALID_STATE;
    }
    if (ctx->scan_active.load()) {
        LOG_W(TAG, "scan_start: already scanning");
        return ERROR_INVALID_STATE;
    }

    ble_scan_clear_results(device);

    struct ble_gap_disc_params disc_params = {};
    disc_params.passive         = 0;
    disc_params.filter_duplicates = 1;

    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    int rc = ble_gap_disc(own_addr_type, 5000, &disc_params, ble_gap_disc_event_handler, device);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        LOG_E(TAG, "ble_gap_disc failed (rc=%d)", rc);
        return ERROR_UNDEFINED;
    }

    ctx->scan_active.store(true);
    {
        struct BtEvent e = {};
        e.type = BT_EVENT_SCAN_STARTED;
        ble_publish_event(device, e);
    }
    return ERROR_NONE;
}

static error_t api_scan_stop(struct Device* device) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_INVALID_STATE;
    ble_gap_disc_cancel();
    ctx->scan_active.store(false);
    struct BtEvent e = {};
    e.type = BT_EVENT_SCAN_FINISHED;
    ble_publish_event(device, e);
    return ERROR_NONE;
}

static bool api_is_scanning(struct Device* device) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    return ctx != nullptr && ctx->scan_active.load();
}

static error_t api_pair(struct Device* /*device*/, const BtAddr /*addr*/) {
    // Pairing handled automatically by NimBLE SM during connection.
    // ENC_CHANGE fires when encryption is established (PAIR_RESULT event).
    return ERROR_NONE;
}

static error_t api_unpair(struct Device* device, const BtAddr addr) {
    // Remove from NimBLE bond store. Settings file removal is handled by
    // the Tactility bluetooth::unpair() wrapper which calls settings::remove().
    ble_addr_t ble_addr = {};
    ble_addr.type = BLE_ADDR_PUBLIC;
    memcpy(ble_addr.val, addr, BT_ADDR_LEN);
    ble_store_util_delete_peer(&ble_addr);
    return ERROR_NONE;
}

static error_t api_get_paired_peers(struct Device* /*device*/, struct BtPeerRecord* /*out*/, size_t* /*count*/) {
    // Paired peer enumeration reads filesystem — handled in Tactility layer.
    return ERROR_NOT_SUPPORTED;
}

static error_t api_connect(struct Device* device, const BtAddr addr, enum BtProfileId profile) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_INVALID_STATE;
    if (profile == BT_PROFILE_HID_DEVICE) {
        return nimble_hid_device_api.start(ctx->hid_device_child, BT_HID_DEVICE_MODE_KEYBOARD);
    } else if (profile == BT_PROFILE_SPP) {
        return ble_spp_start_internal(ctx->serial_child);
    } else if (profile == BT_PROFILE_MIDI) {
        return ble_midi_start_internal(ctx->midi_child);
    }
    // BT_PROFILE_HID_HOST is handled entirely in the Tactility layer.
    return ERROR_NOT_SUPPORTED;
}

static error_t api_disconnect(struct Device* device, const BtAddr addr, enum BtProfileId profile) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_INVALID_STATE;
    if (profile == BT_PROFILE_HID_DEVICE) {
        return nimble_hid_device_api.stop(ctx->hid_device_child);
    } else if (profile == BT_PROFILE_SPP) {
        return nimble_serial_api.stop(ctx->serial_child);
    } else if (profile == BT_PROFILE_MIDI) {
        return nimble_midi_api.stop(ctx->midi_child);
    }
    return ERROR_NOT_SUPPORTED;
}

static error_t api_add_event_callback(struct Device* device, void* cb_ctx, BtEventCallback fn) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !fn) return ERROR_INVALID_ARGUMENT;
    xSemaphoreTake(ctx->cb_mutex, portMAX_DELAY);
    if (ctx->callback_count >= BLE_MAX_CALLBACKS) {
        xSemaphoreGive(ctx->cb_mutex);
        return ERROR_OUT_OF_MEMORY;
    }
    ctx->callbacks[ctx->callback_count].fn  = fn;
    ctx->callbacks[ctx->callback_count].ctx = cb_ctx;
    ctx->callback_count++;
    xSemaphoreGive(ctx->cb_mutex);
    return ERROR_NONE;
}

static error_t api_remove_event_callback(struct Device* device, BtEventCallback fn) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !fn) return ERROR_INVALID_ARGUMENT;
    xSemaphoreTake(ctx->cb_mutex, portMAX_DELAY);
    for (size_t i = 0; i < ctx->callback_count; i++) {
        if (ctx->callbacks[i].fn == fn) {
            // Shift remaining entries down
            for (size_t j = i; j + 1 < ctx->callback_count; j++) {
                ctx->callbacks[j] = ctx->callbacks[j + 1];
            }
            ctx->callback_count--;
            xSemaphoreGive(ctx->cb_mutex);
            return ERROR_NONE;
        }
    }
    xSemaphoreGive(ctx->cb_mutex);
    return ERROR_NOT_FOUND;
}

static error_t api_set_device_name(struct Device* device, const char* name) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !name) return ERROR_INVALID_ARGUMENT;
    if (strlen(name) > BLE_DEVICE_NAME_MAX) return ERROR_INVALID_ARGUMENT;
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    strncpy(ctx->device_name, name, BLE_DEVICE_NAME_MAX);
    ctx->device_name[BLE_DEVICE_NAME_MAX] = '\0';
    if (ctx->radio_state.load() == BT_RADIO_STATE_ON) {
        ble_svc_gap_device_name_set(ctx->device_name);
        // Restart advertising so the new name is broadcast immediately.
        // ble_schedule_adv_restart checks active profiles; no-op if nothing is advertising.
        ble_schedule_adv_restart(device, 0);
    }
    xSemaphoreGive(ctx->radio_mutex);
    return ERROR_NONE;
}

static error_t api_get_device_name(struct Device* device, char* buf, size_t buf_len) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !buf || buf_len == 0) return ERROR_INVALID_ARGUMENT;
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    strncpy(buf, ctx->device_name, buf_len - 1);
    buf[buf_len - 1] = '\0';
    xSemaphoreGive(ctx->radio_mutex);
    return ERROR_NONE;
}

static void api_set_hid_host_active(struct Device* device, bool active) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (ctx) ctx->hid_host_active.store(active);
}

static void api_fire_event(struct Device* device, struct BtEvent event) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (ctx) ble_publish_event(device, event);
}

// ---- BluetoothApi struct ----

const BluetoothApi nimble_bluetooth_api = {
    .get_radio_state        = api_get_radio_state,
    .set_radio_enabled      = api_set_radio_enabled,
    .scan_start             = api_scan_start,
    .scan_stop              = api_scan_stop,
    .is_scanning            = api_is_scanning,
    .pair                   = api_pair,
    .unpair                 = api_unpair,
    .get_paired_peers       = api_get_paired_peers,
    .connect                = api_connect,
    .disconnect             = api_disconnect,
    .add_event_callback     = api_add_event_callback,
    .remove_event_callback  = api_remove_event_callback,
    .set_device_name        = api_set_device_name,
    .get_device_name        = api_get_device_name,
    .set_hid_host_active    = api_set_hid_host_active,
    .fire_event             = api_fire_event,
};

// ---- Driver definitions ----
// Child driver structs (esp32_ble_serial_driver, esp32_ble_midi_driver,
// esp32_ble_hid_device_driver) are defined in their respective sub-module files
// and declared extern in esp32_ble_internal.h.

// ---- Driver lifecycle ----

static void create_child_device(struct Device* parent, const char* name,
                                Driver* drv, struct Device*& out) {
    out = new Device { .name = name, .config = nullptr, .parent = nullptr, .internal = nullptr };
    device_construct(out);
    device_set_parent(out, parent);
    device_set_driver(out, drv);
    device_add(out);
    device_start(out);
}

static void destroy_child_device(struct Device*& child) {
    if (child == nullptr) return;
    device_stop(child);
    device_remove(child);
    device_destruct(child);
    delete child;
    child = nullptr;
}

static error_t esp32_ble_start_device(struct Device* device) {
    BleCtx* ctx = new BleCtx();
    ctx->radio_mutex = xSemaphoreCreateRecursiveMutex();
    ctx->cb_mutex    = xSemaphoreCreateMutex();
    ctx->radio_state.store(BT_RADIO_STATE_OFF);
    ctx->scan_active.store(false);
    ctx->hid_host_active.store(false);
    ctx->callback_count = 0;
    ctx->spp_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->spp_active.store(false);
    ctx->midi_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->midi_active.store(false);
    ctx->midi_use_indicate.store(false);
    ctx->hid_active.store(false);
    ctx->link_encrypted.store(false);
    ctx->pending_reset_count.store(0);
    ctx->midi_keepalive_timer = nullptr;
    ctx->adv_restart_timer    = nullptr;
    ctx->disable_timer        = nullptr;
    // Device name: prefer the Kconfig-supplied TT_DEVICE_NAME, fall back to "Tactility"
    const char* cfg_name = CONFIG_TT_DEVICE_NAME;
    if (cfg_name == nullptr || cfg_name[0] == '\0') cfg_name = "Tactility";
    strncpy(ctx->device_name, cfg_name, BLE_DEVICE_NAME_MAX);
    ctx->device_name[BLE_DEVICE_NAME_MAX] = '\0';
    ctx->device               = device;
    ctx->serial_child         = nullptr;
    ctx->midi_child           = nullptr;
    ctx->hid_device_child     = nullptr;
    ctx->scan_mutex           = xSemaphoreCreateMutex();
    ctx->scan_count           = 0;
    memset(ctx->scan_results, 0, sizeof(ctx->scan_results));

    // Create the disable timer used to dispatch dispatchDisable off the NimBLE host task.
    esp_timer_create_args_t disable_args = {};
    disable_args.callback        = dispatch_disable_timer_cb;
    disable_args.arg             = ctx;
    disable_args.dispatch_method = ESP_TIMER_TASK;
    disable_args.name            = "ble_disable";
    int rc = esp_timer_create(&disable_args, &ctx->disable_timer);
    if (rc != ESP_OK) {
        LOG_E(TAG, "start_device: disable timer create failed (rc=%d)", rc);
    }

    device_set_driver_data(device, ctx);
    s_ctx = ctx;

    // Create child devices for the serial, MIDI and HID device profiles.
    // device_start() on each child will invoke start_device (for serial/midi)
    // which initialises their driver data (BleSppCtx / BleMidiCtx).
    create_child_device(device, "ble_serial",     &esp32_ble_serial_driver,     ctx->serial_child);
    create_child_device(device, "ble_midi",       &esp32_ble_midi_driver,       ctx->midi_child);
    create_child_device(device, "ble_hid_device", &esp32_ble_hid_device_driver, ctx->hid_device_child);

    return ERROR_NONE;
}

static error_t esp32_ble_stop_device(struct Device* device) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_NONE;

    // Destroy child devices before stopping the radio and freeing the context.
    // device_stop() on each child will invoke stop_device (for serial/midi/hid)
    // which frees their driver data.
    destroy_child_device(ctx->hid_device_child);
    destroy_child_device(ctx->midi_child);
    destroy_child_device(ctx->serial_child);

    if (ctx->radio_state.load() != BT_RADIO_STATE_OFF) {
        dispatch_disable(ctx);
    }

    if (ctx->scan_mutex != nullptr) {
        vSemaphoreDelete(ctx->scan_mutex);
        ctx->scan_mutex = nullptr;
    }

    if (ctx->disable_timer != nullptr) {
        esp_timer_stop(ctx->disable_timer);
        esp_timer_delete(ctx->disable_timer);
        ctx->disable_timer = nullptr;
    }

    s_ctx = nullptr;
    device_set_driver_data(device, nullptr);
    delete ctx;
    return ERROR_NONE;
}

// ---- Driver registration ----

Driver esp32_bluetooth_driver = {
    .name         = "esp32_bluetooth",
    .compatible   = (const char*[]) { "espressif,esp32-ble", nullptr },
    .start_device = esp32_ble_start_device,
    .stop_device  = esp32_ble_stop_device,
    .api          = &nimble_bluetooth_api,
    .device_type  = &BLUETOOTH_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

#endif // CONFIG_BT_NIMBLE_ENABLED
