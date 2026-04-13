#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs_mbuf.h>

constexpr auto* TAG = "esp32_ble_midi";
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>

// ---- MIDI device context (stored as driver data of the MIDI child device) ----

struct BleMidiCtx {
    SemaphoreHandle_t                rx_mutex;
    std::deque<std::vector<uint8_t>> rx_queue;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// ---- BLE MIDI UUIDs ----

// 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
const ble_uuid128_t MIDI_SVC_UUID = BLE_UUID128_INIT(
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
);

// 7772E5DB-3868-4112-A1A9-F2669D106BF3
static const ble_uuid128_t MIDI_IO_UUID = BLE_UUID128_INIT(
    0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
    0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77
);

uint16_t midi_io_handle;

// ---- MIDI field accessor implementations ----

static BleCtx* midi_root_ctx(struct Device* device) {
    return ble_get_ctx(device);
}

bool ble_midi_get_active(struct Device* device) {
    return midi_root_ctx(device)->midi_active.load();
}
void ble_midi_set_active(struct Device* device, bool v) {
    midi_root_ctx(device)->midi_active.store(v);
}
uint16_t ble_midi_get_conn_handle(struct Device* device) {
    return midi_root_ctx(device)->midi_conn_handle.load();
}
void ble_midi_set_conn_handle(struct Device* device, uint16_t h) {
    midi_root_ctx(device)->midi_conn_handle.store(h);
}
bool ble_midi_get_use_indicate(struct Device* device) {
    return midi_root_ctx(device)->midi_use_indicate.load();
}
void ble_midi_set_use_indicate(struct Device* device, bool v) {
    midi_root_ctx(device)->midi_use_indicate.store(v);
}

error_t ble_midi_ensure_keepalive(struct Device* device, esp_timer_cb_t cb, uint64_t period_us) {
    BleCtx* ctx = midi_root_ctx(device);
    if (ctx->midi_keepalive_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback        = cb;
        args.arg             = device;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name            = "ble_midi_as";
        int rc = esp_timer_create(&args, &ctx->midi_keepalive_timer);
        if (rc != ESP_OK) {
            LOG_E(TAG, "midi keepalive timer create failed (rc=%d)", rc);
            return ERROR_INVALID_STATE;
        }
    }
    int rc = esp_timer_start_periodic(ctx->midi_keepalive_timer, period_us);
    if (rc != ESP_OK) {
        LOG_E(TAG, "midi keepalive timer start failed (rc=%d)", rc);
        return ERROR_INVALID_STATE;
    }
    return ERROR_NONE;
}

void ble_midi_stop_keepalive(struct Device* device) {
    BleCtx* ctx = midi_root_ctx(device);
    if (ctx->midi_keepalive_timer != nullptr) {
        esp_timer_stop(ctx->midi_keepalive_timer);
    }
}

// midi_chr_access is called with the midi child Device* (set via ble_midi_init_gatt_handles).
static int midi_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt* ctxt, void* arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        LOG_I(TAG, "MIDI RX %u bytes", (unsigned)len);
        struct Device* device = (struct Device*)arg; // midi child device
        BleMidiCtx* mctx = (BleMidiCtx*)device_get_driver_data(device);
        if (mctx != nullptr && len > 0) {
            std::vector<uint8_t> packet(len);
            os_mbuf_copydata(ctxt->om, 0, len, packet.data());
            {
                xSemaphoreTake(mctx->rx_mutex, portMAX_DELAY);
                mctx->rx_queue.push_back(std::move(packet));
                while (mctx->rx_queue.size() > 16) mctx->rx_queue.pop_front();
                xSemaphoreGive(mctx->rx_mutex);
            }
            struct BtEvent e = {};
            e.type = BT_EVENT_MIDI_DATA_RECEIVED;
            ble_publish_event(device, e);
        }
    }
    return 0;
}

struct ble_gatt_chr_def midi_chars[] = {
    {
        .uuid       = &MIDI_IO_UUID.u,
        .access_cb  = midi_chr_access,
        .arg        = nullptr, // set to midi child Device* in ble_midi_init_gatt_handles()
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
        .val_handle = &midi_io_handle,
    },
    { 0 }
};

void ble_midi_init_gatt_handles(struct Device* midi_child) {
    // Store the midi child Device* as the GATT callback arg so that midi_chr_access
    // can retrieve BleMidiCtx via device_get_driver_data without a global pointer.
    // midi_io_handle is written by NimBLE via the val_handle pointer above.
    midi_chars[0].arg = midi_child;
}

// ---- MIDI Active Sensing keepalive ----
// Callback arg is the midi child Device*.

static void midi_keepalive_cb(void* arg) {
    struct Device* device = (struct Device*)arg; // midi child device
    uint16_t conn = ble_midi_get_conn_handle(device);
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    static const uint8_t as_pkt[3] = { 0x80, 0x80, 0xFE };
    struct os_mbuf* om = ble_hs_mbuf_from_flat(as_pkt, 3);
    if (om == nullptr) return;
    int rc = ble_midi_get_use_indicate(device)
        ? ble_gatts_indicate_custom(conn, midi_io_handle, om)
        : ble_gatts_notify_custom(conn, midi_io_handle, om);
    if (rc != 0) os_mbuf_free_chain(om);
}

// ---- MIDI sub-API implementations ----
// All functions receive the midi child Device*.

static error_t midi_start(struct Device* device) {
    ble_midi_set_active(device, true);
    // Create 2-second periodic Active Sensing timer to prevent Windows BLE MIDI
    // driver from declaring the connection idle and disconnecting (~8-10 s timeout).
    error_t rc = ble_midi_ensure_keepalive(device, midi_keepalive_cb, 2'000'000);
    if (rc != ERROR_NONE) return rc;
    ble_start_advertising(device, &MIDI_SVC_UUID);
    return ERROR_NONE;
}

error_t ble_midi_start_internal(struct Device* midi_child) {
    return midi_start(midi_child);
}

static error_t midi_stop(struct Device* device) {
    ble_midi_set_active(device, false);
    ble_midi_stop_keepalive(device);
    uint16_t conn = ble_midi_get_conn_handle(device);
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        ble_midi_set_conn_handle(device, BLE_HS_CONN_HANDLE_NONE);
    }
    if (!ble_spp_get_active(device) && !ble_hid_get_active(device)) {
        ble_gap_adv_stop();
    }
    return ERROR_NONE;
}

static error_t midi_send(struct Device* device, const uint8_t* msg, size_t len) {
    uint16_t conn = ble_midi_get_conn_handle(device);
    if (conn == BLE_HS_CONN_HANDLE_NONE) return ERROR_INVALID_STATE;
    // BLE MIDI 2-byte header: [0x80|(ts_high&0x3F)][0x80|(ts_low&0x7F)]
    uint8_t header[2] = { 0x80, 0x80 };
    struct os_mbuf* om = ble_hs_mbuf_from_flat(header, 2);
    if (om == nullptr) return ERROR_INVALID_STATE;
    if (os_mbuf_append(om, msg, len) != 0) {
        os_mbuf_free_chain(om);
        LOG_E(TAG, "midi_send: mbuf append failed");
        return ERROR_INVALID_STATE;
    }
    LOG_I(TAG, "midi_send %u bytes (indicate=%d)", (unsigned)len, (int)ble_midi_get_use_indicate(device));
    int rc = ble_midi_get_use_indicate(device)
        ? ble_gatts_indicate_custom(conn, midi_io_handle, om)
        : ble_gatts_notify_custom(conn, midi_io_handle, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        LOG_E(TAG, "midi_send failed rc=%d", rc);
    }
    return (rc == 0) ? ERROR_NONE : ERROR_INVALID_STATE;
}

static bool midi_is_connected(struct Device* device) {
    return ble_midi_get_conn_handle(device) != BLE_HS_CONN_HANDLE_NONE;
}

extern const BtMidiApi nimble_midi_api = {
    .start        = midi_start,
    .stop         = midi_stop,
    .send         = midi_send,
    .is_connected = midi_is_connected,
};

// ---- MIDI child driver lifecycle ----

static error_t esp32_ble_midi_start_device(struct Device* device) {
    BleMidiCtx* mctx = new BleMidiCtx();
    mctx->rx_mutex = xSemaphoreCreateMutex();
    device_set_driver_data(device, mctx);
    return ERROR_NONE;
}

static error_t esp32_ble_midi_stop_device(struct Device* device) {
    BleMidiCtx* mctx = (BleMidiCtx*)device_get_driver_data(device);
    if (mctx != nullptr) {
        vSemaphoreDelete(mctx->rx_mutex);
        delete mctx;
        device_set_driver_data(device, nullptr);
    }
    return ERROR_NONE;
}

Driver esp32_ble_midi_driver = {
    .name         = "esp32_ble_midi",
    .compatible   = nullptr,
    .start_device = esp32_ble_midi_start_device,
    .stop_device  = esp32_ble_midi_stop_device,
    .api          = &nimble_midi_api,
    .device_type  = &BLUETOOTH_MIDI_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

#pragma GCC diagnostic pop

#endif // CONFIG_BT_NIMBLE_ENABLED
