#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>

#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <cstring>

#define TAG "esp32_ble_hid"
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// ---- HID device context (stored as driver data of the HID device child) ----

#include <atomic>

struct BleHidDeviceCtx {
    std::atomic<uint16_t> hid_conn_handle;
};

// ---- Module globals ----

BleHidProfile current_hid_profile = BleHidProfile::None;
uint16_t hid_appearance = 0x03C1; // Keyboard (default)
const uint8_t* active_hid_rpt_map    = nullptr;
size_t         active_hid_rpt_map_len = 0;

// GATT attribute handles — written by NimBLE via val_handle pointers below.
uint16_t hid_kb_input_handle;
uint16_t hid_consumer_input_handle;
uint16_t hid_mouse_input_handle;
uint16_t hid_gamepad_input_handle;

// ---- Static UUID objects for HID 16-bit UUIDs ----

static const ble_uuid16_t UUID16_RPT_REF    = BLE_UUID16_INIT(0x2908);
static const ble_uuid16_t UUID16_HID_INFO   = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t UUID16_RPT_MAP    = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t UUID16_HID_CTRL   = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t UUID16_HID_REPORT = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t UUID16_PROTO_MODE = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t UUID16_HID_SVC    = BLE_UUID16_INIT(0x1812);

static uint8_t hid_protocol_mode = 0x01; // 0x00=Boot, 0x01=Report

// ============================================================================
// Per-profile HID Report Maps
// ============================================================================

// Keyboard + Consumer (IDs 1 and 2)
static const uint8_t hid_rpt_map_kb_consumer[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02,
    0x75, 0x03, 0x95, 0x01, 0x91, 0x01,
    0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
    0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

// Mouse only (ID 1, 4 bytes)
static const uint8_t hid_rpt_map_mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x85, 0x01,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

// Keyboard + Consumer + Mouse (IDs 1, 2, 3)
static const uint8_t hid_rpt_map_kb_mouse[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02,
    0x75, 0x03, 0x95, 0x01, 0x91, 0x01,
    0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
    0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x85, 0x03,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

// Gamepad only (ID 1, 8 bytes)
static const uint8_t hid_rpt_map_gamepad[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02,
    0xC0,
};

// ---- Per-profile Report Reference descriptor data ----
// Format: {Report ID, Report Type} — Type: 1=Input, 2=Output

static const uint8_t rpt_ref_kbc_kb_in[2]  = {1, 1};
static const uint8_t rpt_ref_kbc_cs_in[2]  = {2, 1};
static const uint8_t rpt_ref_kbc_kb_out[2] = {1, 2};
static const uint8_t rpt_ref_ms_in[2]      = {1, 1};
static const uint8_t rpt_ref_kbm_kb_in[2]  = {1, 1};
static const uint8_t rpt_ref_kbm_cs_in[2]  = {2, 1};
static const uint8_t rpt_ref_kbm_ms_in[2]  = {3, 1};
static const uint8_t rpt_ref_kbm_kb_out[2] = {1, 2};
static const uint8_t rpt_ref_gp_in[2]      = {1, 1};

// ---- HID GATT callbacks ----

static int hid_dsc_access(uint16_t /*conn_handle*/, uint16_t /*attr_handle*/,
                          struct ble_gatt_access_ctxt* ctxt, void* arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        const uint8_t* data = (const uint8_t*)arg;
        int rc = os_mbuf_append(ctxt->om, data, 2);
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int hid_chr_access(uint16_t /*conn_handle*/, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt* ctxt, void* /*arg*/) {
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            if (uuid16 == 0x2A4A) {
                static const uint8_t hid_info[4] = { 0x11, 0x01, 0x00, 0x02 };
                int rc = os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
                return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            if (uuid16 == 0x2A4B) {
                if (active_hid_rpt_map == nullptr || active_hid_rpt_map_len == 0)
                    return BLE_ATT_ERR_UNLIKELY;
                int rc = os_mbuf_append(ctxt->om, active_hid_rpt_map, active_hid_rpt_map_len);
                return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            if (uuid16 == 0x2A4E) {
                int rc = os_mbuf_append(ctxt->om, &hid_protocol_mode, 1);
                return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            if (uuid16 == 0x2A4D) {
                static const uint8_t zeros[8] = {};
                size_t report_len = 8;
                if (attr_handle == hid_consumer_input_handle) report_len = 2;
                else if (attr_handle == hid_mouse_input_handle) report_len = 4;
                int rc = os_mbuf_append(ctxt->om, zeros, report_len);
                return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }

        case BLE_GATT_ACCESS_OP_WRITE_CHR: {
            if (uuid16 == 0x2A4C) return 0; // HID Control Point — no action
            if (uuid16 == 0x2A4E) {
                if (OS_MBUF_PKTLEN(ctxt->om) >= 1) {
                    os_mbuf_copydata(ctxt->om, 0, 1, &hid_protocol_mode);
                    LOG_I(TAG, "Protocol Mode -> %u", hid_protocol_mode);
                }
                return 0;
            }
            if (uuid16 == 0x2A4D) {
                if (OS_MBUF_PKTLEN(ctxt->om) >= 1) {
                    uint8_t leds = 0;
                    os_mbuf_copydata(ctxt->om, 0, 1, &leds);
                    LOG_I(TAG, "KB LED state: 0x%02x", leds);
                }
                return 0;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

// ---- Per-profile HID descriptor arrays ----

static struct ble_gatt_dsc_def hid_kbc_kb_dscs[]  = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbc_kb_in  }, { 0 } };
static struct ble_gatt_dsc_def hid_kbc_cs_dscs[]  = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbc_cs_in  }, { 0 } };
static struct ble_gatt_dsc_def hid_kbc_out_dscs[] = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbc_kb_out }, { 0 } };
static struct ble_gatt_dsc_def hid_ms_dscs[]      = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_ms_in      }, { 0 } };
static struct ble_gatt_dsc_def hid_kbm_kb_dscs[]  = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbm_kb_in  }, { 0 } };
static struct ble_gatt_dsc_def hid_kbm_cs_dscs[]  = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbm_cs_in  }, { 0 } };
static struct ble_gatt_dsc_def hid_kbm_ms_dscs[]  = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbm_ms_in  }, { 0 } };
static struct ble_gatt_dsc_def hid_kbm_out_dscs[] = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_kbm_kb_out }, { 0 } };
static struct ble_gatt_dsc_def hid_gp_dscs[]      = { { .uuid = &UUID16_RPT_REF.u, .att_flags = BLE_ATT_F_READ, .access_cb = hid_dsc_access, .arg = (void*)rpt_ref_gp_in      }, { 0 } };

// ---- Per-profile HID characteristic arrays ----

static struct ble_gatt_chr_def hid_chars_kb_consumer[] = {
    { .uuid = &UUID16_HID_INFO.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_RPT_MAP.u,    .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_HID_CTRL.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_PROTO_MODE.u, .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbc_kb_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_kb_input_handle },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbc_cs_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_consumer_input_handle },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbc_out_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { 0 }
};

static struct ble_gatt_chr_def hid_chars_mouse[] = {
    { .uuid = &UUID16_HID_INFO.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_RPT_MAP.u,    .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_HID_CTRL.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_PROTO_MODE.u, .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_ms_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_mouse_input_handle },
    { 0 }
};

static struct ble_gatt_chr_def hid_chars_kb_mouse[] = {
    { .uuid = &UUID16_HID_INFO.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_RPT_MAP.u,    .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_HID_CTRL.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_PROTO_MODE.u, .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbm_kb_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_kb_input_handle },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbm_cs_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_consumer_input_handle },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbm_ms_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_mouse_input_handle },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_kbm_out_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { 0 }
};

static struct ble_gatt_chr_def hid_chars_gamepad[] = {
    { .uuid = &UUID16_HID_INFO.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_RPT_MAP.u,    .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &UUID16_HID_CTRL.u,   .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_PROTO_MODE.u, .access_cb = hid_chr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = &UUID16_HID_REPORT.u, .access_cb = hid_chr_access, .descriptors = hid_gp_dscs,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &hid_gamepad_input_handle },
    { 0 }
};

// ---- Per-profile GATT service arrays ----

static const struct ble_gatt_svc_def gatt_svcs_none[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &NUS_SVC_UUID.u,  .characteristics = nus_chars_with_handle },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &MIDI_SVC_UUID.u, .characteristics = midi_chars },
    { 0 }
};
static const struct ble_gatt_svc_def gatt_svcs_kb_consumer[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &NUS_SVC_UUID.u,   .characteristics = nus_chars_with_handle },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &MIDI_SVC_UUID.u,  .characteristics = midi_chars },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID16_HID_SVC.u, .characteristics = hid_chars_kb_consumer },
    { 0 }
};
static const struct ble_gatt_svc_def gatt_svcs_mouse[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &NUS_SVC_UUID.u,   .characteristics = nus_chars_with_handle },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &MIDI_SVC_UUID.u,  .characteristics = midi_chars },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID16_HID_SVC.u, .characteristics = hid_chars_mouse },
    { 0 }
};
static const struct ble_gatt_svc_def gatt_svcs_kb_mouse[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &NUS_SVC_UUID.u,   .characteristics = nus_chars_with_handle },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &MIDI_SVC_UUID.u,  .characteristics = midi_chars },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID16_HID_SVC.u, .characteristics = hid_chars_kb_mouse },
    { 0 }
};
static const struct ble_gatt_svc_def gatt_svcs_gamepad[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &NUS_SVC_UUID.u,   .characteristics = nus_chars_with_handle },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &MIDI_SVC_UUID.u,  .characteristics = midi_chars },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID16_HID_SVC.u, .characteristics = hid_chars_gamepad },
    { 0 }
};

// ---- HID field accessor implementations ----

static BleCtx* hid_root_ctx(struct Device* device) {
    return ble_get_ctx(device);
}

bool ble_hid_get_active(struct Device* device) {
    return hid_root_ctx(device)->hid_active.load();
}
void ble_hid_set_active(struct Device* device, bool v) {
    hid_root_ctx(device)->hid_active.store(v);
}
uint16_t ble_hid_get_conn_handle(struct Device* device) {
    if (device == nullptr) return (uint16_t)BLE_HS_CONN_HANDLE_NONE;
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    return hid_ctx ? hid_ctx->hid_conn_handle.load() : (uint16_t)BLE_HS_CONN_HANDLE_NONE;
}
void ble_hid_set_conn_handle(struct Device* device, uint16_t h) {
    if (device == nullptr) return;
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx) hid_ctx->hid_conn_handle.store(h);
}

// ---- GATT profile switch ----
// device must be the HID device child Device*.

void ble_hid_switch_profile(struct Device* device, BleHidProfile profile) {
    if (profile == current_hid_profile) return;
    LOG_I(TAG, "switchGattProfile: %d -> %d", (int)current_hid_profile, (int)profile);

    ble_gap_adv_stop();

    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx && hid_ctx->hid_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(hid_ctx->hid_conn_handle.load(), BLE_ERR_REM_USER_CONN_TERM);
        hid_ctx->hid_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    }

    ble_gatts_reset();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    const struct ble_gatt_svc_def* svcs = gatt_svcs_none;
    const uint8_t* new_rpt_map     = nullptr;
    size_t         new_rpt_map_len = 0;
    switch (profile) {
        case BleHidProfile::KbConsumer: svcs = gatt_svcs_kb_consumer; new_rpt_map = hid_rpt_map_kb_consumer; new_rpt_map_len = sizeof(hid_rpt_map_kb_consumer); break;
        case BleHidProfile::Mouse:      svcs = gatt_svcs_mouse;        new_rpt_map = hid_rpt_map_mouse;        new_rpt_map_len = sizeof(hid_rpt_map_mouse);        break;
        case BleHidProfile::KbMouse:    svcs = gatt_svcs_kb_mouse;     new_rpt_map = hid_rpt_map_kb_mouse;     new_rpt_map_len = sizeof(hid_rpt_map_kb_mouse);     break;
        case BleHidProfile::Gamepad:    svcs = gatt_svcs_gamepad;      new_rpt_map = hid_rpt_map_gamepad;      new_rpt_map_len = sizeof(hid_rpt_map_gamepad);      break;
        default:                        svcs = gatt_svcs_none;                                                                                                      break;
    }

    int rc = ble_gatts_count_cfg(svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(svcs);
        if (rc != 0) {
            LOG_E(TAG, "switchGattProfile: gatts_add_svcs failed rc=%d", rc);
            return; // don't update profile — GATT state is inconsistent
        }
    } else {
        LOG_E(TAG, "switchGattProfile: gatts_count_cfg failed rc=%d", rc);
        return;
    }

    // ble_gatts_add_svcs() only adds definitions to a pending list.
    // ble_gatts_start() converts them into live ATT entries.
    // Without this call, all GATT reads return ATT errors and Windows
    // cannot install the HID driver → Phase 2 reconnect never occurs.
    rc = ble_gatts_start();
    if (rc != 0) {
        LOG_E(TAG, "switchGattProfile: gatts_start failed rc=%d", rc);
        return;
    }

    active_hid_rpt_map     = new_rpt_map;
    active_hid_rpt_map_len = new_rpt_map_len;

    ble_svc_gap_device_name_set(CONFIG_TT_DEVICE_NAME);
    ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);
    ble_svc_gatt_changed(0, 0xFFFF);

    current_hid_profile = profile;
}

void ble_hid_init_gatt() {
    current_hid_profile    = BleHidProfile::None;
    active_hid_rpt_map     = nullptr;
    active_hid_rpt_map_len = 0;
    int rc = ble_gatts_count_cfg(gatt_svcs_none);
    if (rc != 0) {
        LOG_E(TAG, "gatts_count_cfg failed (rc=%d)", rc);
    } else {
        rc = ble_gatts_add_svcs(gatt_svcs_none);
        if (rc != 0) {
            LOG_E(TAG, "gatts_add_svcs failed (rc=%d)", rc);
        }
    }
}

// ---- HID Device sub-API implementations ----
// All functions receive the HID device child Device* and operate on BleHidDeviceCtx
// stored as its driver data.

static error_t hid_device_start(struct Device* device, enum BtHidDeviceMode mode) {
    // Create driver data for this HID session.
    BleHidDeviceCtx* hid_ctx = new BleHidDeviceCtx();
    hid_ctx->hid_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
    device_set_driver_data(device, hid_ctx);

    BleHidProfile profile;
    uint16_t appearance;
    switch (mode) {
        case BT_HID_DEVICE_MODE_MOUSE:
            profile    = BleHidProfile::Mouse;
            appearance = 0x03C2;
            break;
        case BT_HID_DEVICE_MODE_KEYBOARD_MOUSE:
            profile    = BleHidProfile::KbMouse;
            appearance = 0x03C0;
            break;
        case BT_HID_DEVICE_MODE_GAMEPAD:
            profile    = BleHidProfile::Gamepad;
            appearance = 0x03C4;
            break;
        default: // BT_HID_DEVICE_MODE_KEYBOARD
            profile    = BleHidProfile::KbConsumer;
            appearance = 0x03C1;
            break;
    }

    hid_appearance = appearance;
    ble_hid_switch_profile(device, profile);
    ble_hid_set_active(device, true);
    ble_start_advertising_hid(device, hid_appearance);
    return ERROR_NONE;
}

static error_t hid_device_stop(struct Device* device) {
    ble_hid_set_active(device, false);
    ble_gap_adv_stop();
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    uint16_t conn = hid_ctx ? hid_ctx->hid_conn_handle.load() : (uint16_t)BLE_HS_CONN_HANDLE_NONE;
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        // Connected: terminate and let the DISCONNECT handler switch profile to None.
        // ble_gatts_mutable() returns false while a connection is live, so calling
        // switch_profile here would assert inside ble_svc_gap_init().
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        // Do NOT clear hid_conn_handle or delete hid_ctx:
        // the DISCONNECT handler in esp32_ble.cpp uses hid_conn_handle for was_hid detection.
        // esp32_ble_hid_device_stop_device() (device lifecycle) will free hid_ctx later.
    } else {
        // Not connected: GATT is mutable, switch profile immediately.
        if (current_hid_profile != BleHidProfile::None) {
            ble_hid_switch_profile(device, BleHidProfile::None);
        }
        delete hid_ctx;
        device_set_driver_data(device, nullptr);
    }
    return ERROR_NONE;
}

static error_t hid_device_send_key(struct Device* device, uint8_t keycode, bool pressed) {
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx == nullptr || hid_ctx->hid_conn_handle.load() == BLE_HS_CONN_HANDLE_NONE) {
        return ERROR_INVALID_STATE;
    }
    uint8_t report[8] = {};
    if (pressed) report[2] = keycode;
    struct os_mbuf* om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om == nullptr) return ERROR_INVALID_STATE;
    int rc = ble_gatts_notify_custom(hid_ctx->hid_conn_handle.load(), hid_kb_input_handle, om);
    if (rc != 0) os_mbuf_free_chain(om);
    return (rc == 0) ? ERROR_NONE : ERROR_INVALID_STATE;
}

static error_t hid_notify(uint16_t conn_handle, uint16_t attr_handle,
                           const uint8_t* data, size_t len) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) return ERROR_INVALID_STATE;
    if (attr_handle == 0) return ERROR_INVALID_STATE; // handle not registered for current profile
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (om == nullptr) return ERROR_INVALID_STATE;
    int rc = ble_gatts_notify_custom(conn_handle, attr_handle, om);
    if (rc != 0) os_mbuf_free_chain(om);
    return (rc == 0) ? ERROR_NONE : ERROR_INVALID_STATE;
}

static error_t hid_device_send_keyboard(struct Device* device, const uint8_t* report, size_t len) {
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx == nullptr) return ERROR_INVALID_STATE;
    uint8_t buf[8] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_notify(hid_ctx->hid_conn_handle.load(), hid_kb_input_handle, buf, sizeof(buf));
}

static error_t hid_device_send_consumer(struct Device* device, const uint8_t* report, size_t len) {
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx == nullptr) return ERROR_INVALID_STATE;
    uint8_t buf[2] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_notify(hid_ctx->hid_conn_handle.load(), hid_consumer_input_handle, buf, sizeof(buf));
}

static error_t hid_device_send_mouse(struct Device* device, const uint8_t* report, size_t len) {
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx == nullptr) return ERROR_INVALID_STATE;
    uint8_t buf[4] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_notify(hid_ctx->hid_conn_handle.load(), hid_mouse_input_handle, buf, sizeof(buf));
}

static error_t hid_device_send_gamepad(struct Device* device, const uint8_t* report, size_t len) {
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    if (hid_ctx == nullptr) return ERROR_INVALID_STATE;
    uint8_t buf[8] = {};
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    return hid_notify(hid_ctx->hid_conn_handle.load(), hid_gamepad_input_handle, buf, sizeof(buf));
}

static bool hid_device_is_connected(struct Device* device) {
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    return hid_ctx != nullptr && hid_ctx->hid_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE;
}

extern const BtHidDeviceApi nimble_hid_device_api = {
    .start         = hid_device_start,
    .stop          = hid_device_stop,
    .send_key      = hid_device_send_key,
    .send_keyboard = hid_device_send_keyboard,
    .send_consumer = hid_device_send_consumer,
    .send_mouse    = hid_device_send_mouse,
    .send_gamepad  = hid_device_send_gamepad,
    .is_connected  = hid_device_is_connected,
};

// ---- HID device child driver lifecycle ----

static error_t esp32_ble_hid_device_stop_device(struct Device* device) {
    // Safety cleanup: free any BleHidDeviceCtx that was not deleted by hid_device_stop()
    // (e.g. if the BLE device is stopped while HID is still connected).
    BleHidDeviceCtx* hid_ctx = (BleHidDeviceCtx*)device_get_driver_data(device);
    delete hid_ctx; // safe even if nullptr
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

Driver esp32_ble_hid_device_driver = {
    .name         = "esp32-ble-hid-device",
    .compatible   = nullptr,
    .start_device = nullptr,
    .stop_device  = esp32_ble_hid_device_stop_device,
    .api          = &nimble_hid_device_api,
    .device_type  = &BLUETOOTH_HID_DEVICE_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

#pragma GCC diagnostic pop

#endif // CONFIG_BT_NIMBLE_ENABLED
