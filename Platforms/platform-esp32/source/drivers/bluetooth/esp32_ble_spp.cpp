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

constexpr auto* TAG = "esp32_ble_spp";
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>

// ---- SPP device context (stored as driver data of the serial child device) ----

struct BleSppCtx {
    SemaphoreHandle_t                rx_mutex;
    std::deque<std::vector<uint8_t>> rx_queue;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// ---- NUS (Nordic UART Service) UUIDs ----

// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
const ble_uuid128_t NUS_SVC_UUID = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
);

// 6E400002 RX (write from client → device)
static const ble_uuid128_t NUS_RX_UUID = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
);

// 6E400003 TX (notify device → client)
static const ble_uuid128_t NUS_TX_UUID = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
);

uint16_t nus_tx_handle;

// nus_chr_access is called with the serial child Device* (set via ble_spp_init_gatt_handles).
static int nus_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt* ctxt, void* arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        LOG_I(TAG, "NUS RX %u bytes", (unsigned)len);
        struct Device* device = (struct Device*)arg; // serial child device
        BleSppCtx* sctx = (BleSppCtx*)device_get_driver_data(device);
        if (sctx != nullptr && len > 0) {
            std::vector<uint8_t> packet(len);
            os_mbuf_copydata(ctxt->om, 0, len, packet.data());
            {
                xSemaphoreTake(sctx->rx_mutex, portMAX_DELAY);
                sctx->rx_queue.push_back(std::move(packet));
                while (sctx->rx_queue.size() > 16) sctx->rx_queue.pop_front();
                xSemaphoreGive(sctx->rx_mutex);
            }
            struct BtEvent e = {};
            e.type = BT_EVENT_SPP_DATA_RECEIVED;
            ble_publish_event(device, e);
        }
    }
    return 0;
}

struct ble_gatt_chr_def nus_chars_with_handle[] = {
    {
        .uuid      = &NUS_RX_UUID.u,
        .access_cb = nus_chr_access,
        .arg       = nullptr, // set to serial child Device* in ble_spp_init_gatt_handles()
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid       = &NUS_TX_UUID.u,
        .access_cb  = nus_chr_access,
        .arg        = nullptr, // set to serial child Device* in ble_spp_init_gatt_handles()
        .flags      = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &nus_tx_handle,
    },
    { 0 }
};

void ble_spp_init_gatt_handles(struct Device* serial_child) {
    // Store the serial child Device* as the GATT callback arg so that nus_chr_access
    // can retrieve BleSppCtx via device_get_driver_data without a global pointer.
    // nus_tx_handle is written by NimBLE via the val_handle pointer above.
    nus_chars_with_handle[0].arg = serial_child;
    nus_chars_with_handle[1].arg = serial_child;
}

// ---- SPP field accessor implementations ----

static BleCtx* spp_root_ctx(struct Device* device) {
    return ble_get_ctx(device);
}

bool ble_spp_get_active(struct Device* device) {
    return spp_root_ctx(device)->spp_active.load();
}
void ble_spp_set_active(struct Device* device, bool v) {
    spp_root_ctx(device)->spp_active.store(v);
}
uint16_t ble_spp_get_conn_handle(struct Device* device) {
    return spp_root_ctx(device)->spp_conn_handle.load();
}
void ble_spp_set_conn_handle(struct Device* device, uint16_t h) {
    spp_root_ctx(device)->spp_conn_handle.store(h);
}

// ---- SPP sub-API implementations ----
// All functions receive the serial child Device*.

static error_t spp_start(struct Device* device) {
    ble_spp_set_active(device, true);
    ble_start_advertising(device, &NUS_SVC_UUID);
    return ERROR_NONE;
}

error_t ble_spp_start_internal(struct Device* serial_child) {
    return spp_start(serial_child);
}

static error_t spp_stop(struct Device* device) {
    ble_spp_set_active(device, false);
    uint16_t conn = ble_spp_get_conn_handle(device);
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        ble_spp_set_conn_handle(device, BLE_HS_CONN_HANDLE_NONE);
    }
    // Do NOT restart advertising after user-initiated stop — restarting name-only
    // advertising causes bonded Windows hosts to auto-reconnect in a tight loop.
    if (!ble_midi_get_active(device) && !ble_hid_get_active(device)) {
        ble_gap_adv_stop();
    }
    return ERROR_NONE;
}

static error_t spp_write(struct Device* device, const uint8_t* data, size_t len, size_t* written) {
    uint16_t conn = ble_spp_get_conn_handle(device);
    if (conn == BLE_HS_CONN_HANDLE_NONE) {
        if (written) *written = 0;
        return ERROR_INVALID_STATE;
    }
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (om == nullptr) {
        if (written) *written = 0;
        return ERROR_INVALID_STATE;
    }
    int rc = ble_gatts_notify_custom(conn, nus_tx_handle, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        if (written) *written = 0;
        return ERROR_INVALID_STATE;
    }
    if (written) *written = len;
    return ERROR_NONE;
}

static error_t spp_read(struct Device* device, uint8_t* data, size_t max_len, size_t* read_out) {
    BleSppCtx* sctx = (BleSppCtx*)device_get_driver_data(device);
    if (sctx == nullptr || data == nullptr || max_len == 0) {
        if (read_out) *read_out = 0;
        return ERROR_NONE;
    }
    xSemaphoreTake(sctx->rx_mutex, portMAX_DELAY);
    if (sctx->rx_queue.empty()) {
        xSemaphoreGive(sctx->rx_mutex);
        if (read_out) *read_out = 0;
        return ERROR_NONE;
    }
    auto& front = sctx->rx_queue.front();
    size_t copy_len = std::min(front.size(), max_len);
    memcpy(data, front.data(), copy_len);
    sctx->rx_queue.pop_front();
    xSemaphoreGive(sctx->rx_mutex);
    if (read_out) *read_out = copy_len;
    return ERROR_NONE;
}

static bool spp_is_connected(struct Device* device) {
    return ble_spp_get_conn_handle(device) != BLE_HS_CONN_HANDLE_NONE;
}

extern const BtSerialApi nimble_serial_api = {
    .start        = spp_start,
    .stop         = spp_stop,
    .write        = spp_write,
    .read         = spp_read,
    .is_connected = spp_is_connected,
};

// ---- Serial child driver lifecycle ----

static error_t esp32_ble_serial_start_device(struct Device* device) {
    BleSppCtx* sctx = new BleSppCtx();
    sctx->rx_mutex = xSemaphoreCreateMutex();
    device_set_driver_data(device, sctx);
    return ERROR_NONE;
}

static error_t esp32_ble_serial_stop_device(struct Device* device) {
    BleSppCtx* sctx = (BleSppCtx*)device_get_driver_data(device);
    if (sctx != nullptr) {
        vSemaphoreDelete(sctx->rx_mutex);
        delete sctx;
        device_set_driver_data(device, nullptr);
    }
    return ERROR_NONE;
}

Driver esp32_ble_serial_driver = {
    .name         = "esp32_ble_serial",
    .compatible   = nullptr,
    .start_device = esp32_ble_serial_start_device,
    .stop_device  = esp32_ble_serial_stop_device,
    .api          = &nimble_serial_api,
    .device_type  = &BLUETOOTH_SERIAL_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

#pragma GCC diagnostic pop

#endif // CONFIG_BT_NIMBLE_ENABLED
