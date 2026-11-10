#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>
#include <bluetooth/esp32_ble_hid_host.h>

#include <tactility/device.h>
#include <tactility/driver.h>

#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
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
#include <tactility/memory.h>
#include <esp_mac.h>
#if CONFIG_IDF_TARGET_ESP32S31
#include <esp_bt.h>
#endif
#include <esp_timer.h>

#if defined(CONFIG_ESP_HOSTED_ENABLED)
#include <esp_hosted.h>
extern "C" {
#include <esp_hosted_misc.h>
}
#endif

// ble_store_config_init() is not declared in the public header in some IDF versions.
extern "C" void ble_store_config_init(void);

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
    mutex_lock(&ctx->subscriptionsMutex);
    for (BtEventSubscription* sub = ctx->subscriptions; sub != nullptr; sub = sub->internal.next) {
        mutex_lock(&sub->internal.ring_mutex);
        if (sub->internal.count < BT_EVENT_QUEUE_CAPACITY) {
            uint8_t tail = (sub->internal.head + sub->internal.count) % BT_EVENT_QUEUE_CAPACITY;
            sub->internal.queue[tail] = event;
            sub->internal.count++;
        }
        mutex_unlock(&sub->internal.ring_mutex);
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&ctx->subscriptionsMutex);
}

// ---- Advertising restart helper ----

// Delay before on_sync() restarts advertising for an already-active profile.
constexpr uint64_t ADV_ON_SYNC_DELAY_US = 100'000;

static void adv_restart_callback(void* arg) {
    struct Device* device = (struct Device*)arg;
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx->radio_state.load() != BT_RADIO_STATE_ON) return;

    // gap_mutex (not radio_mutex - this can run on the host task and must never wait on a mutex
    // disable can hold across nimble_port_stop()) serializes this against hid/spp/midi_device_
    // start()/_stop(), which touch the same GAP/GATT state from another task.
    xSemaphoreTake(ctx->gap_mutex, portMAX_DELAY);
    uint16_t hid_conn = ble_hid_get_conn_handle(ctx->hid_device_child);
    if (ctx->midi_active.load() && ctx->midi_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
        ble_start_advertising(device, &MIDI_SVC_UUID);
    } else if (ctx->spp_active.load() && ctx->spp_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
        ble_start_advertising(device, &NUS_SVC_UUID);
    } else if (ctx->hid_active.load() && hid_conn == BLE_HS_CONN_HANDLE_NONE) {
        ble_start_advertising_hid(device, hid_appearance);
    }
    // No active profile: stay silent. A profile-less advertisement would carry the last
    // identity, so a bonded host would reconnect to a device with no app behind it.
    xSemaphoreGive(ctx->gap_mutex);
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
                ctx->conn_count.fetch_add(1);
                // Claim inbound links for HID immediately, so a stop can also terminate a
                // discovery-only link that has no subscription yet. Only an inbound link can be
                // HID here, and never while SPP or MIDI own the advertisement.
                if (ctx->hid_active.load() && !ctx->spp_active.load() && !ctx->midi_active.load() &&
                    ble_hid_get_conn_handle(ctx->hid_device_child) == BLE_HS_CONN_HANDLE_NONE) {
                    struct ble_gap_conn_desc desc = {};
                    if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0 &&
                        desc.role == BLE_GAP_ROLE_SLAVE) {
                        ble_hid_set_conn_handle(ctx->hid_device_child, event->connect.conn_handle);
                    }
                }
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
            if (ctx->conn_count.load() > 0) ctx->conn_count.fetch_sub(1);
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
                if (!ble_hid_switch_profile(ctx->hid_device_child, BleHidProfile::None)) {
                    LOG_E(TAG, "disconnect: switch to None failed — GATT state inconsistent");
                }
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

/**
 * Install the advertising address of the active identity: the controller's public BD_ADDR
 * with the identity hash mixed in, as a static random address. Also used when the identity
 * changes on a running radio, before any advertising starts.
 */
static void apply_identity_own_addr(BleCtx* ctx) {
    if (ctx->identity_hash == 0) {
        ctx->own_addr_type.store(BLE_OWN_ADDR_PUBLIC);
        return;
    }

    uint8_t base[BT_ADDR_LEN] = {};
    if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, base, nullptr) != 0) {
        // Fallback for controllers that have not reported their BD_ADDR over HCI yet. On a
        // hosted setup this is the host's MAC, which is still stable across reboots.
        esp_read_mac(base, ESP_MAC_BT);
    }

    uint8_t addr[BT_ADDR_LEN];
    if (!ble_identity_derive_addr(ctx->identity_hash, base, addr)) {
        ctx->own_addr_type.store(BLE_OWN_ADDR_PUBLIC);
        return;
    }

    int rc = ble_hs_id_set_rnd(addr);
    if (rc != 0) {
        // Advertising falls back to the public address, so peers share one identity.
        ctx->own_addr_type.store(BLE_OWN_ADDR_PUBLIC);
        LOG_E(TAG, "identity: set random address failed (rc=%d), advertising with public address", rc);
        return;
    }

    ctx->own_addr_type.store(BLE_OWN_ADDR_RANDOM);
    LOG_I(TAG, "identity %08lx advertising as %02x:%02x:%02x:%02x:%02x:%02x (static random)",
             (unsigned long)ctx->identity_hash,
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

/**
 * Apply the pending identity to a radio that is already on, see esp32_ble_internal.h. Runs on
 * the caller's task, never on the NimBLE host task.
 */
bool ble_identity_apply_pending(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx == nullptr) return false;

    // radio_mutex then gap_mutex, the order dispatch_enable() and api_set_device_name() use.
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    if (ctx->radio_state.load() != BT_RADIO_STATE_ON) {
        // dispatch_enable() resolves the identity from the pending values when it comes up.
        xSemaphoreGive(ctx->radio_mutex);
        return true;
    }
    const char* pending = (ctx->identity[0] != '\0') ? ctx->identity : ctx->device_name;
    const uint32_t pending_hash = ble_identity_hash(pending);
    if (pending_hash == ctx->identity_hash) {
        xSemaphoreGive(ctx->radio_mutex);
        return true;
    }

    xSemaphoreTake(ctx->gap_mutex, portMAX_DELAY);

    // A background scan is not a profile, abort it so it does not hold the GAP or the identity.
    ble_scan_abort(device);

    // The bond set and the address only change while nothing is connected or active. A live
    // link was authenticated against the old set, and an active profile owns the identity.
    const bool idle = !ble_gap_adv_active() && !ble_gap_disc_active() && !ble_gap_conn_active() &&
                      ctx->conn_count.load() == 0 &&
                      ctx->name_res_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE &&
                      !ctx->spp_active.load() && !ctx->midi_active.load() && !ctx->hid_active.load() &&
                      !ble_hid_get_host_active(device);
    if (!idle) {
        xSemaphoreGive(ctx->gap_mutex);
        xSemaphoreGive(ctx->radio_mutex);
        LOG_I(TAG, "identity: radio busy, '%s' joins the current identity '%s'",
              pending, ctx->identity_resolved);
        return false;
    }

    if (!ble_identity_bond_slot_activate(pending_hash)) {
        xSemaphoreGive(ctx->gap_mutex);
        xSemaphoreGive(ctx->radio_mutex);
        LOG_W(TAG, "identity: could not activate the bond set of '%s'; staying on '%s'",
              pending, ctx->identity_resolved);
        return false;
    }
    // Drop the cached NVS records and load the newly active bond set.
    ble_store_config_init();

    ctx->identity_hash = pending_hash;
    strncpy(ctx->identity_resolved, pending, BLE_IDENTITY_MAX);
    ctx->identity_resolved[BLE_IDENTITY_MAX] = '\0';
    // Install the identity address before any advertising starts.
    apply_identity_own_addr(ctx);

    xSemaphoreGive(ctx->gap_mutex);
    xSemaphoreGive(ctx->radio_mutex);
    LOG_I(TAG, "identity switched to '%s' (%08lx) on the running radio",
          ctx->identity_resolved, (unsigned long)pending_hash);
    return true;
}

bool ble_prepare_profile_start(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    // Name resolution keeps opening central connections to read peer names.
    ble_scan_abort(device);

    // A cancelled connect attempt clears asynchronously on the NimBLE host task, and an open
    // name-read link is terminated the same way. Runs on the caller's task, never on the host
    // task. The GATT switch and the bond swap both need no link at all.
    const int64_t deadline = esp_timer_get_time() + 500'000;
    uint16_t terminated_link = BLE_HS_CONN_HANDLE_NONE;
    while (ble_gap_conn_active() || ble_gap_disc_active() ||
           ctx->name_res_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE) {
        uint16_t name_res_link = ctx->name_res_conn_handle.load();
        if (name_res_link != BLE_HS_CONN_HANDLE_NONE && name_res_link != terminated_link) {
            // The link may have completed after ble_scan_abort(), end it now.
            ble_gap_terminate(name_res_link, BLE_ERR_REM_USER_CONN_TERM);
            terminated_link = name_res_link;
        }
        if (esp_timer_get_time() > deadline) {
            LOG_W(TAG, "profile start: GAP still busy after 500 ms (disc=%d conn=%d name_res=%d)",
                  (int)ble_gap_disc_active(), (int)ble_gap_conn_active(),
                  (int)(ctx->name_res_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

static void on_sync() {
    LOG_I(TAG, "NimBLE synced (nus_tx=%u midi_io=%u hid_kb=%u hid_cs=%u hid_ms=%u hid_gp=%u)",
             nus_tx_handle, midi_io_handle,
             hid_kb_input_handle, hid_consumer_input_handle,
             hid_mouse_input_handle, hid_gamepad_input_handle);
    BleCtx* ctx = s_ctx;
    if (ctx == nullptr) return;

    // Must happen before ble_hs_util_ensure_addr()/ble_hs_id_infer_auto() below: they cache
    // which address type the host uses, and advertising additionally reads own_addr_type.
    apply_identity_own_addr(ctx);

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

    // Deferred, not synchronous: an app reacting to the event just published above (e.g.
    // starting HID) races this to set hid/spp/midi_active. Starting advertising here and
    // tearing it down again a few ms later for a profile switch was observed to corrupt the
    // NimBLE host's HCI event pool (crash in os_memblock_get, called from
    // lld_adv_end_ind_handler_hack). The delay gives that app time to react first.
    // The callback only restarts advertising for profiles that are already active (it does
    // nothing when none is), so this also recovers advertising after a controller reset.
    ble_schedule_adv_restart(ctx->device, ADV_ON_SYNC_DELAY_US);
}

static void dispatch_disable_timer_cb(void* arg) {
    BleCtx* ctx = (BleCtx*)arg;
    // Matches api_set_radio_enabled()'s locking so the give-up teardown can't run
    // concurrently with a user-initiated dispatch_enable()/dispatch_disable() on
    // the same ctx (both would otherwise race nimble_port_stop()/deinit()).
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    dispatch_disable(ctx);
    xSemaphoreGive(ctx->radio_mutex);
}

// Runs on the NimBLE host task, but only once the event loop is idle again —
// i.e. strictly after the ble_hs_reset()/ble_hs_sync() call that triggered the
// giveup has fully returned (this event was queued behind it on g_eventq_dflt).
// Safe to kick disable_timer here: no in-flight NimBLE call is touching
// ble_hs_timer on this task anymore. See giveup_event's comment in BleCtx.
static void giveup_event_cb(struct ble_npl_event* ev) {
    BleCtx* ctx = (BleCtx*)ble_npl_event_get_arg(ev);
    if (ctx != nullptr && ctx->disable_timer != nullptr) {
        esp_timer_start_once(ctx->disable_timer, 0);
    }
}

static void on_reset(int reason) {
    LOG_W(TAG, "NimBLE host reset (reason=%d)", reason);
    BleCtx* ctx = s_ctx;
    if (ctx == nullptr) return;

    if (ctx->radio_state.load() == BT_RADIO_STATE_ON_PENDING) {
        int count = ctx->pending_reset_count.fetch_add(1) + 1;
        if (count == 3) {
            LOG_E(TAG, "BT controller unresponsive after 3 resets — giving up");
            // Do NOT fire disable_timer directly from here: ble_hs_reset() (our
            // caller) unconditionally falls through to ble_hs_sync() right after
            // reset_cb returns, which touches the unlocked global ble_hs_timer.
            // Queue the giveup behind that on NimBLE's own event queue instead —
            // see giveup_event_cb().
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &ctx->giveup_event);
        }
    }
}

static void host_task(void* param) {
    LOG_I(TAG, "BLE host task started");
    nimble_port_run();
    // Signal that this task will not process any further NimBLE events (g_eventq_dflt
    // is fully drained past the stop sentinel) before dispatch_disable() is allowed to
    // proceed to nimble_port_deinit(). nimble_port_stop()'s own semaphore only confirms
    // the stop event was serviced, not that the host task has stopped dequeuing events —
    // a fresh HCI-ack-wait timeout can still queue and run ble_hs_ev_reset in that gap,
    // racing against deinit freeing ble_hs_timer's callout (crash: ble_hs_timer_sched ->
    // ble_npl_callout_is_active on a freed/zeroed callout).
    if (s_ctx != nullptr && s_ctx->host_task_done_sem != nullptr) {
        xSemaphoreGive(s_ctx->host_task_done_sem);
    }
    // nimble_port_deinit() is called by dispatch_disable() after nimble_port_stop() returns.
    // Delete this task directly instead of calling nimble_port_freertos_deinit(): that helper
    // goes through esp_nimble_disable(), which does vTaskDelete(host_task_h) on the port's
    // global handle. If a re-enable already ran, host_task_h points at the NEW host task and
    // this old task would delete it (intermittent crash when toggling the radio off/on fast).
    vTaskDelete(nullptr);
}

// ---- Advertising helpers ----

// Address type advertising runs with: the identity's static random address once on_sync()
// installed it, else the controller's public address.
static uint8_t advertising_addr_type(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    return (ctx != nullptr) ? ctx->own_addr_type.load() : BLE_OWN_ADDR_PUBLIC;
}

// One scan response holds a single name field: 31-byte payload minus the length and type bytes.
constexpr uint8_t SCAN_RSP_NAME_MAX = 29;

/** Longest prefix of @a name that fits in @a max_len bytes without splitting a UTF-8 character. */
static uint8_t utf8_prefix_length(const char* name, uint8_t max_len) {
    size_t len = strlen(name);
    if (len <= max_len) return (uint8_t)len;
    len = max_len;
    // The first excluded byte must not be a continuation byte.
    while (len > 0 && (static_cast<uint8_t>(name[len]) & 0xC0) == 0x80) {
        --len;
    }
    return (uint8_t)len;
}

/**
 * Put the complete device name in the scan response. A name too long for one response is
 * truncated and marked as a shortened name, otherwise the device would advertise without one.
 */
static void set_scan_response_name(const char* name) {
    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.name             = (const uint8_t*)name;
    rsp.name_len         = (uint8_t)strlen(name);
    rsp.name_is_complete = 1;
    int rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc == 0) return;

    rsp.name_len         = utf8_prefix_length(name, SCAN_RSP_NAME_MAX);
    rsp.name_is_complete = 0;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        LOG_W(TAG, "scan response name failed rc=%d", rc);
    }
}

void ble_start_advertising(struct Device* device, const ble_uuid128_t* svc_uuid) {
    ble_gap_adv_stop();

    // Sync the GAP name from ctx so bluetooth_set_device_name() is honoured regardless of when
    // it was called.
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx) ble_svc_gap_device_name_set(ctx->device_name);

    const char* name = ble_svc_gap_device_name();
    uint8_t name_len = (uint8_t)strlen(name);

    ble_uuid128_t uuid_copy = *svc_uuid;
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128             = &uuid_copy;
    fields.num_uuids128         = 1;
    fields.uuids128_is_complete = 1;
    // Complete name only. A shortened name is identical for every identity sharing a prefix,
    // the scan response below covers names that do not fit.
    fields.name                 = (const uint8_t*)name;
    fields.name_len             = name_len;
    fields.name_is_complete     = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        LOG_I(TAG, "startAdvertising: name doesn't fit the adv packet (rc=%d), using scan response", rc);
        fields.name     = nullptr;
        fields.name_len = 0;
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            LOG_E(TAG, "startAdvertising: set_fields failed rc=%d", rc);
            return;
        }
    }

    set_scan_response_name(name);
    LOG_I(TAG, "startAdvertising: UUID mode (uuid[0..3]=%02x%02x%02x%02x)",
             svc_uuid->value[0], svc_uuid->value[1],
             svc_uuid->value[2], svc_uuid->value[3]);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 160; // 100 ms
    adv_params.itvl_max  = 240; // 150 ms

    rc = ble_gap_adv_start(advertising_addr_type(device), nullptr, BLE_HS_FOREVER,
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
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx) ble_svc_gap_device_name_set(ctx->device_name);

    const char* name  = ble_svc_gap_device_name();
    uint8_t name_len  = (uint8_t)strlen(name);

    static const ble_uuid16_t hid_uuid16 = BLE_UUID16_INIT(0x1812);

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance           = appearance;
    fields.appearance_is_present = 1;
    fields.uuids16              = &hid_uuid16;
    fields.num_uuids16          = 1;
    fields.uuids16_is_complete  = 1;
    // Complete name only, the scan response below covers names that do not fit.
    fields.name                 = (const uint8_t*)name;
    fields.name_len             = name_len;
    fields.name_is_complete     = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        LOG_I(TAG, "startAdvertisingHid: name doesn't fit the adv packet (rc=%d), using scan response", rc);
        fields.name     = nullptr;
        fields.name_len = 0;
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            LOG_E(TAG, "startAdvertisingHid: set_fields failed rc=%d", rc);
            return;
        }
    }

    set_scan_response_name(name);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 160;
    adv_params.itvl_max  = 240;

    rc = ble_gap_adv_start(advertising_addr_type(device), nullptr, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, device);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        LOG_E(TAG, "startAdvertisingHid: adv_start failed rc=%d", rc);
    } else {
        LOG_I(TAG, "startAdvertisingHid: OK");
    }
}

// ---- Dispatch helpers ----

static void free_scan_buffers(BleCtx* ctx) {
    xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
    memory_free(ctx->scan_results);
    memory_free(ctx->scan_addrs);
    ctx->scan_results = nullptr;
    ctx->scan_addrs   = nullptr;
    ctx->scan_count   = 0;
    xSemaphoreGive(ctx->scan_mutex);
}

static void dispatch_enable(BleCtx* ctx) {
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

#if defined(CONFIG_ESP_HOSTED_ENABLED)
    if (esp_hosted_connect_to_slave() != ESP_OK) {
        LOG_W(TAG, "esp_hosted_connect_to_slave failed");
    }
    if (esp_hosted_bt_controller_init() != ESP_OK) {
        LOG_W(TAG, "esp_hosted_bt_controller_init failed");
    }
    if (esp_hosted_bt_controller_enable() != ESP_OK) {
        LOG_W(TAG, "esp_hosted_bt_controller_enable failed");
    }
#endif

    constexpr MemoryPolicy scan_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
    ctx->scan_results = static_cast<BtPeerRecord*>(memory_calloc_with_policy(BLE_SCAN_RESULTS_MAX, sizeof(BtPeerRecord), &scan_policy));
    ctx->scan_addrs   = static_cast<ble_addr_t*>(memory_calloc_with_policy(BLE_SCAN_RESULTS_MAX, sizeof(ble_addr_t), &scan_policy));
    ctx->scan_count   = 0;
    bool scan_alloc_failed = ctx->scan_results == nullptr || ctx->scan_addrs == nullptr;
    xSemaphoreGive(ctx->scan_mutex);

    // Resolve the identity and make its bond set the active one before the NimBLE host comes
    // up: ble_store_config_init() below loads the bond records from NVS, so the swap has to be
    // committed while the host is still down (and before it can advertise).
    const char* identity = (ctx->identity[0] != '\0') ? ctx->identity : ctx->device_name;
    const uint32_t identity_hash = ble_identity_hash(identity);
    ctx->conn_count.store(0);
    ctx->name_res_abort.store(false);
    ctx->name_res_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    if (!scan_alloc_failed) {
        if (ble_identity_bond_slot_activate(identity_hash)) {
            ctx->identity_hash = identity_hash;
            strncpy(ctx->identity_resolved, identity, BLE_IDENTITY_MAX);
            ctx->identity_resolved[BLE_IDENTITY_MAX] = '\0';
        } else {
            // Keep the identity whose bonds are actually active. A profile start retries the swap.
            LOG_W(TAG, "identity: '%s' not activated, staying on '%s'", identity,
                  ctx->identity_resolved[0] != '\0' ? ctx->identity_resolved : "public address");
        }
    }

    int rc = scan_alloc_failed ? -1 : 0;
    if (!scan_alloc_failed) {
#if CONFIG_IDF_TARGET_ESP32S31
        // IDF 6.1 preview: esp_bt_controller_deinit() asserts in
        // btdm_osal_elem_mempool_deinit() even after a clean host deinit + controller disable,
        // so the controller is kept initialized and only disabled between sessions.
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
            esp_bt_controller_config_t config_opts = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            rc = esp_bt_controller_init(&config_opts);
        }
        if (rc == 0 && esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            rc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        }
        // esp_nimble_init() resets the NimBLE port's internal deinit flag itself.
        if (rc == 0) {
            rc = esp_nimble_init();
        }
#else
        rc = nimble_port_init();
#endif
    }
    if (rc != 0) {
        if (scan_alloc_failed) {
            LOG_E(TAG, "Scan buffer allocation failed");
        } else {
            LOG_E(TAG, "nimble_port_init failed (rc=%d)", rc);
        }

        enum BtRadioState failure_state = BT_RADIO_STATE_OFF;
#if CONFIG_IDF_TARGET_ESP32S31
        // If the controller was enabled before esp_nimble_init() failed, roll it back so the
        // reported radio state matches the hardware and a later enable starts from a known state.
        if (!scan_alloc_failed && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            esp_err_t disable_result = esp_bt_controller_disable();
            if (disable_result != ESP_OK) {
                LOG_E(TAG, "controller rollback failed: %s", esp_err_to_name(disable_result));
                failure_state = BT_RADIO_STATE_ON;
            }
        }
#endif

        free_scan_buffers(ctx);
        ctx->radio_state.store(failure_state);
        struct BtEvent e = {};
        e.type = BT_EVENT_RADIO_STATE_CHANGED;
        e.radio_state = failure_state;
        ble_publish_event(ctx->device, e);
        return;
    }

    // Re-init the giveup event every enable cycle, npl_funcs only exists after
    // nimble_port_init(). nimble_port_deinit() frees the event pool that giveup_event's block
    // came from, so any state left in giveup_event is stale. Zero the whole event object before
    // ble_npl_event_init(): on targets where ble_npl_event embeds btdm_osal_event (e.g. S31),
    // init populates the embedded struct in place, while pointer-based ports reuse a stale pool
    // pointer otherwise.
    std::memset(&ctx->giveup_event, 0, sizeof(ctx->giveup_event));
    ble_npl_event_init(&ctx->giveup_event, giveup_event_cb, ctx);

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
    // Drain any stale "done" signal left over from a previous cycle (e.g. if the
    // last dispatch_disable() timed out waiting on it — see xSemaphoreTake below
    // with pdMS_TO_TICKS(2000)) so dispatch_disable() only ever consumes the
    // completion signal for THIS host task instance.
    if (ctx->host_task_done_sem != nullptr) {
        xSemaphoreTake(ctx->host_task_done_sem, 0);
    }
    // Start NimBLE host task (on_sync will fire when ready)
    nimble_port_freertos_init(host_task);
}

static void dispatch_disable(BleCtx* ctx) {
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
    // nimble_port_stop()'s internal semaphore only confirms the stop sentinel event was
    // serviced — not that host_task()'s call to nimble_port_run() has returned. A reset
    // event (e.g. from an independent HCI-ack-wait timeout) can still be running on the
    // host task in that gap. Wait for host_task() to explicitly signal completion before
    // freeing anything NimBLE still references (ble_hs_timer et al in nimble_port_deinit()),
    // otherwise ble_hs_timer_sched() on the host task can dereference a freed callout —
    // see host_task() for the crash this fixes.
    if (ctx->host_task_done_sem != nullptr) {
        if (xSemaphoreTake(ctx->host_task_done_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
            LOG_W(TAG, "host task did not signal completion in time");
        }
    }

#if CONFIG_IDF_TARGET_ESP32S31
    // The giveup event is allocated from the controller's OSAL mempool by ble_npl_event_init()
    // (on S31 struct ble_npl_event wraps btdm_osal_event), so it has to be returned before the
    // controller deinit, which asserts that every mempool element was freed.
    ble_npl_eventq_remove(nimble_port_get_dflt_eventq(), &ctx->giveup_event);
    ble_npl_event_deinit(&ctx->giveup_event);
#endif
#if defined(CONFIG_ESP_HOSTED_ENABLED)
    // Close the gate NOW — after nimble_port_stop() returns the NimBLE host task has
    // exited and nimble_port_deinit() is about to zero npl_funcs. Any HCI packet
    // arriving after this point must not reach ble_transport_alloc_evt().
    ble_hci_gate_set_active(false);
    if (!ble_hci_gate_wait_idle(20)) {
        LOG_W(TAG, "HCI gate drain timed out");
    }
#endif
    enum BtRadioState final_state = BT_RADIO_STATE_OFF;
#if CONFIG_IDF_TARGET_ESP32S31
    esp_err_t host_deinit_result = esp_nimble_deinit();
    if (host_deinit_result != ESP_OK) {
        LOG_E(TAG, "host deinit failed: %s", esp_err_to_name(host_deinit_result));
    }

    // Attempt the controller disable independently: a failed host deinit must not leave the
    // radio enabled while callers believe it is off.
    esp_err_t disable_result = esp_bt_controller_disable();
    if (disable_result != ESP_OK) {
        LOG_E(TAG, "controller disable failed: %s", esp_err_to_name(disable_result));
        // Report the radio as still on so a later off request can retry the disable.
        final_state = BT_RADIO_STATE_ON;
    }
#else
    nimble_port_deinit();
#endif
    free_scan_buffers(ctx);

#if defined(CONFIG_ESP_HOSTED_ENABLED)
    // Symmetric with the enable-side esp_hosted_bt_controller_init/enable() calls.
    if (esp_hosted_bt_controller_disable() != ESP_OK) {
        LOG_W(TAG, "esp_hosted_bt_controller_disable failed");
    }
    if (esp_hosted_bt_controller_deinit(true) != ESP_OK) {
        LOG_W(TAG, "esp_hosted_bt_controller_deinit failed");
    }
#endif

    ctx->spp_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->spp_active.store(false);
    ctx->midi_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->midi_active.store(false);
    ctx->hid_active.store(false);
    // No link survives a host restart, so linked state has to be cleared here too.
    ctx->hid_host_active.store(false);
    ctx->conn_count.store(0);
    ctx->name_res_abort.store(false);
    ctx->name_res_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
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

    ctx->radio_state.store(final_state);
    {
        struct BtEvent e = {};
        e.type = BT_EVENT_RADIO_STATE_CHANGED;
        e.radio_state = final_state;
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
    // A new scan is a new name-resolution round.
    ctx->name_res_abort.store(false);

    struct ble_gap_disc_params disc_params = {};
    disc_params.passive         = 0;
    disc_params.filter_duplicates = 1;

    // Central role always scans from the public address. The identity address is the peripheral
    // identity, using it here would invalidate bonds with peers we act as a host for.
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &disc_params, ble_gap_disc_event_handler, device);
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

static bool api_is_peripheral_active(struct Device* device) {
    BleCtx* ctx = ble_get_ctx(device);
    return ctx != nullptr &&
           (ctx->hid_active.load() || ctx->spp_active.load() || ctx->midi_active.load());
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
    // Deleting a bond persists the store to NVS
    if (!ble_run_file_operation([](void* context) {
        ble_store_util_delete_peer(static_cast<ble_addr_t*>(context));
    }, &ble_addr)) {
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t api_get_paired_peers(struct Device* /*device*/, struct BtPeerRecord* /*out*/, size_t* /*count*/) {
    // Paired peer enumeration reads filesystem — handled in Tactility layer.
    return ERROR_NOT_SUPPORTED;
}

static error_t api_host_connect(struct Device* device, const BtAddr addr) {
    return ble_hid_host_connect(device, addr);
}

static error_t api_host_disconnect(struct Device* /*device*/, const BtAddr /*addr*/) {
    return ble_hid_host_disconnect();
}

static error_t api_event_subscribe(struct Device* device, BtEventSubscription* sub, TaskEventGroup* event_group) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !sub || !event_group) return ERROR_INVALID_ARGUMENT;

    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&ctx->subscriptionsMutex);

    // Avoid cyclic subscription list that would loop forever
    for (BtEventSubscription* existing = ctx->subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&ctx->subscriptionsMutex);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->internal.event_group = event_group;
    sub->bit = bit;
    sub->internal.next = ctx->subscriptions;
    ctx->subscriptions = sub;
    mutex_unlock(&ctx->subscriptionsMutex);
    return ERROR_NONE;
}

static error_t api_event_unsubscribe(struct Device* device, BtEventSubscription* sub) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !sub) return ERROR_INVALID_ARGUMENT;

    error_t result = ERROR_NOT_FOUND;
    mutex_lock(&ctx->subscriptionsMutex);
    for (BtEventSubscription** link = &ctx->subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&ctx->subscriptionsMutex);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }
    return result;
}

static error_t api_set_device_name(struct Device* device, const char* name) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx || !name) return ERROR_INVALID_ARGUMENT;
    if (strlen(name) > BLE_DEVICE_NAME_MAX) return ERROR_INVALID_ARGUMENT;
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    strncpy(ctx->device_name, name, BLE_DEVICE_NAME_MAX);
    ctx->device_name[BLE_DEVICE_NAME_MAX] = '\0';
    if (ctx->radio_state.load() == BT_RADIO_STATE_ON) {
        // A rename does not change the running session's address or bonds. Those follow at the
        // next profile start if the radio is idle, or at the next radio cycle.
        const char* pending_identity = (ctx->identity[0] != '\0') ? ctx->identity : ctx->device_name;
        if (ctx->identity_hash != ble_identity_hash(pending_identity)) {
            LOG_I(TAG, "Device name is now '%s'; identity '%s' applies from the next profile "
                "start or radio cycle", ctx->device_name, ctx->identity_resolved);
        }
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

static error_t api_set_identity(struct Device* device, const char* identity) {
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx == nullptr) return ERROR_INVALID_ARGUMENT;
    if (identity != nullptr && strlen(identity) > BLE_IDENTITY_MAX) {
        return ERROR_INVALID_ARGUMENT;
    }

    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    // Address and bond set can only change with nothing connected. A mid-session change is
    // pending until the next profile start or radio cycle.
    if (identity == nullptr || identity[0] == '\0') {
        ctx->identity[0] = '\0';
    } else {
        strncpy(ctx->identity, identity, BLE_IDENTITY_MAX);
        ctx->identity[BLE_IDENTITY_MAX] = '\0';
    }
    xSemaphoreGive(ctx->radio_mutex);
    return ERROR_NONE;
}

static error_t api_get_identity(struct Device* device, char* buf, size_t buf_len) {
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx == nullptr || buf == nullptr || buf_len == 0) return ERROR_INVALID_ARGUMENT;
    xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
    // While the radio is up, report the identity the running session resolved.
    const char* identity = (ctx->radio_state.load() != BT_RADIO_STATE_OFF &&
                            ctx->identity_resolved[0] != '\0')
        ? ctx->identity_resolved
        : ((ctx->identity[0] != '\0') ? ctx->identity : ctx->device_name);
    strncpy(buf, identity, buf_len - 1);
    buf[buf_len - 1] = '\0';
    xSemaphoreGive(ctx->radio_mutex);
    return ERROR_NONE;
}

// ---- BluetoothApi struct ----

const BluetoothApi nimble_bluetooth_api = {
    .get_radio_state        = api_get_radio_state,
    .set_radio_enabled      = api_set_radio_enabled,
    .scan_start             = api_scan_start,
    .scan_stop              = api_scan_stop,
    .is_scanning            = api_is_scanning,
    .is_peripheral_active   = api_is_peripheral_active,
    .pair                   = api_pair,
    .unpair                 = api_unpair,
    .get_paired_peers       = api_get_paired_peers,
    .host_connect           = api_host_connect,
    .host_disconnect        = api_host_disconnect,
    .event_subscribe        = api_event_subscribe,
    .event_unsubscribe      = api_event_unsubscribe,
    .set_device_name        = api_set_device_name,
    .get_device_name        = api_get_device_name,
    .set_identity           = api_set_identity,
    .get_identity           = api_get_identity,
};

// ---- Driver definitions ----
// Child driver structs (esp32_ble_serial_driver, esp32_ble_midi_driver,
// esp32_ble_hid_device_driver) are defined in their respective sub-module files
// and declared extern in esp32_ble_internal.h.

// ---- Driver lifecycle ----

static void create_child_device(struct Device* parent, const char* name,
                                Driver* drv, struct Device*& out) {
    out = new Device { .address = 0, .name = name, .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr };
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
    ctx->radio_mutex = xSemaphoreCreateMutex();
    ctx->gap_mutex = xSemaphoreCreateMutex();
    mutex_construct(&ctx->subscriptionsMutex);
    ctx->subscriptions = nullptr;
    ctx->radio_state.store(BT_RADIO_STATE_OFF);
    ctx->scan_active.store(false);
    ctx->hid_host_active.store(false);
    ctx->spp_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->spp_active.store(false);
    ctx->midi_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->midi_active.store(false);
    ctx->midi_use_indicate.store(false);
    ctx->hid_active.store(false);
    ctx->link_encrypted.store(false);
    ctx->conn_count.store(0);
    ctx->name_res_abort.store(false);
    ctx->name_res_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    ctx->pending_reset_count.store(0);
    ctx->midi_keepalive_timer = nullptr;
    ctx->adv_restart_timer    = nullptr;
    ctx->disable_timer        = nullptr;
    // Device name: prefer the Kconfig-supplied TT_DEVICE_NAME, fall back to "Tactility"
    const char* cfg_name = CONFIG_TT_DEVICE_NAME;
    if (cfg_name == nullptr || cfg_name[0] == '\0') cfg_name = "Tactility";
    strncpy(ctx->device_name, cfg_name, BLE_DEVICE_NAME_MAX);
    ctx->device_name[BLE_DEVICE_NAME_MAX] = '\0';
    // No explicit identity yet: dispatch_enable() falls back to device_name.
    ctx->identity[0]          = '\0';
    ctx->identity_hash        = 0;
    ctx->own_addr_type.store(BLE_OWN_ADDR_PUBLIC);
    ctx->device               = device;
    ctx->serial_child         = nullptr;
    ctx->midi_child           = nullptr;
    ctx->hid_device_child     = nullptr;
    ctx->scan_mutex           = xSemaphoreCreateMutex();
    ctx->scan_results         = nullptr;
    ctx->scan_addrs           = nullptr;
    ctx->scan_count           = 0;
    ctx->host_task_done_sem   = xSemaphoreCreateBinary();
    if (ctx->host_task_done_sem == nullptr) {
        LOG_E(TAG, "start_device: host_task_done_sem create failed");
    }

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

    return ble_hid_host_init(device);
}

static error_t esp32_ble_stop_device(struct Device* device) {
    BleCtx* ctx = (BleCtx*)device_get_driver_data(device);
    if (!ctx) return ERROR_NONE;

    // Destroy child devices before stopping the radio and freeing the context.
    // device_stop() on each child will invoke stop_device (for serial/midi/hid)
    // which frees their driver data.
    ble_hid_host_deinit();
    destroy_child_device(ctx->hid_device_child);
    destroy_child_device(ctx->midi_child);
    destroy_child_device(ctx->serial_child);

    if (ctx->radio_state.load() != BT_RADIO_STATE_OFF) {
        xSemaphoreTake(ctx->radio_mutex, portMAX_DELAY);
        dispatch_disable(ctx);
        xSemaphoreGive(ctx->radio_mutex);
    }

    if (ctx->scan_mutex != nullptr) {
        vSemaphoreDelete(ctx->scan_mutex);
        ctx->scan_mutex = nullptr;
    }

    if (ctx->host_task_done_sem != nullptr) {
        vSemaphoreDelete(ctx->host_task_done_sem);
        ctx->host_task_done_sem = nullptr;
    }

    if (ctx->disable_timer != nullptr) {
        esp_timer_stop(ctx->disable_timer);
        esp_timer_delete(ctx->disable_timer);
        ctx->disable_timer = nullptr;
    }

    if (ctx->gap_mutex != nullptr) {
        vSemaphoreDelete(ctx->gap_mutex);
        ctx->gap_mutex = nullptr;
    }

    // Release any subscribers that never unsubscribed: device_stop() doesn't wait for apps still
    // using this device, so a later bluetooth_event_unsubscribe() would find no ctx and skip
    // releasing the bit.
    mutex_lock(&ctx->subscriptionsMutex);
    for (BtEventSubscription* sub = ctx->subscriptions; sub != nullptr;) {
        BtEventSubscription* next = sub->internal.next;
        // Force-close, don't destruct: a concurrent bluetooth_event_poll() may hold/await this
        // same lock (see BtEventSubscription::internal::closed).
        mutex_lock(&sub->internal.ring_mutex);
        sub->internal.closed = true;
        mutex_unlock(&sub->internal.ring_mutex);
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
        sub = next;
    }
    ctx->subscriptions = nullptr;
    mutex_unlock(&ctx->subscriptionsMutex);
    mutex_destruct(&ctx->subscriptionsMutex);

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
    .probe = nullptr,
    .api          = &nimble_bluetooth_api,
    .device_type  = &BLUETOOTH_TYPE,
    .internal     = nullptr,
};

#endif // CONFIG_BT_NIMBLE_ENABLED
