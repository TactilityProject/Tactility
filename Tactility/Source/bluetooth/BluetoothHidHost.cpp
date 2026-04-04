#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/bluetooth/BluetoothPairedDevice.h>
#include <Tactility/bluetooth/BluetoothPrivate.h>

#include <Tactility/Assets.h>
#include <Tactility/Logger.h>
#include <Tactility/Tactility.h>
#include <Tactility/lvgl/Keyboard.h>
#include <Tactility/lvgl/LvglSync.h>

#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lvgl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#define TAG "BtHidHost"
#include <esp_log.h>

namespace tt::bluetooth {

static const auto LOGGER = Logger("BtHidHost");

// ---- Report type ----

enum class HidReportType : uint8_t { Unknown = 0, Keyboard, Mouse, Consumer };

struct HidHostInputRpt {
    uint16_t valHandle;
    uint16_t cccdHandle;
    uint16_t rptRefHandle;
    uint8_t  reportId;
    HidReportType type;
};

struct HidHostCtx {
    uint16_t connHandle        = BLE_HS_CONN_HANDLE_NONE;
    uint16_t hidSvcStart       = 0;
    uint16_t hidSvcEnd         = 0;
    std::vector<HidHostInputRpt> inputRpts;
    std::vector<uint16_t> allChrDefHandles;
    int subscribeIdx           = 0;
    int dscDiscIdx             = 0;
    int rptRefReadIdx          = 0;
    uint16_t rptMapHandle      = 0;
    std::vector<uint8_t> rptMap;
    bool securityInitiated     = false;
    bool typeResolutionDone    = false;
    bool readyBlockFired       = false;
    lv_indev_t* kbIndev        = nullptr;
    lv_indev_t* mouseIndev     = nullptr;
    lv_obj_t*   mouseCursor    = nullptr;
    std::array<uint8_t, 6> peerAddr = {};
};

// ---- Globals ----

static std::unique_ptr<HidHostCtx> hid_host_ctx;
static QueueHandle_t hid_host_key_queue = nullptr;
static uint8_t hid_host_prev_keys[6] = {};
static esp_timer_handle_t hid_enc_retry_timer = nullptr;

static std::atomic<int32_t> hid_host_mouse_x{0};
static std::atomic<int32_t> hid_host_mouse_y{0};
static std::atomic<bool>    hid_host_mouse_btn{false};
static std::atomic<bool>    hid_host_mouse_active{false};

#define HID_HOST_KEY_QUEUE_SIZE 64
struct HidHostKeyEvt { uint32_t key; bool pressed; };

// ---- Forward declarations ----

static void hidHostSubscribeNext(HidHostCtx& ctx);
static void hidHostStartRptRefRead(HidHostCtx& ctx);
static void hidHostReadReportMap(HidHostCtx& ctx);
static uint16_t getDescEndHandle(const HidHostCtx& ctx, uint16_t valHandle);

// ---- Keycode mapping ----

static uint32_t hidHostMapKeycode(uint8_t mod, uint8_t kc) {
    bool shift = (mod & 0x22) != 0;
    switch (kc) {
        case 0x28: return LV_KEY_ENTER;
        case 0x29: return LV_KEY_ESC;
        case 0x2A: return LV_KEY_BACKSPACE;
        case 0x4C: return LV_KEY_DEL;
        case 0x2B: return shift ? (uint32_t)LV_KEY_PREV : (uint32_t)LV_KEY_NEXT;
        case 0x52: return LV_KEY_UP;
        case 0x51: return LV_KEY_DOWN;
        case 0x50: return LV_KEY_LEFT;
        case 0x4F: return LV_KEY_RIGHT;
        case 0x4A: return LV_KEY_HOME;
        case 0x4D: return LV_KEY_END;
        default: break;
    }
    if (kc >= 0x04 && kc <= 0x1D) {
        uint32_t c = static_cast<uint32_t>('a' + (kc - 0x04));
        return shift ? (c - 0x20) : c;
    }
    if (kc >= 0x1E && kc <= 0x27) {
        static const char nums[]  = "1234567890";
        static const char snums[] = "!@#$%^&*()";
        int i = kc - 0x1E;
        return shift ? static_cast<uint32_t>(snums[i]) : static_cast<uint32_t>(nums[i]);
    }
    if (kc == 0x2C) return ' ';
    return 0;
}

static void hidHostKeyboardReadCb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    if (!hid_host_key_queue) { data->state = LV_INDEV_STATE_RELEASED; return; }
    HidHostKeyEvt evt = {};
    if (xQueueReceive(hid_host_key_queue, &evt, 0) == pdTRUE) {
        data->key   = evt.key;
        data->state = evt.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = (uxQueueMessagesWaiting(hid_host_key_queue) > 0);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void hidHostHandleKeyboardReport(const uint8_t* data, uint16_t len) {
    if (len < 3 || !hid_host_key_queue) return;
    uint8_t mod = data[0];
    const uint8_t* curr = &data[2];
    int nkeys = std::min((int)(len - 2), 6);

    for (int i = 0; i < 6; i++) {
        uint8_t kc = hid_host_prev_keys[i];
        if (kc == 0) continue;
        bool still = false;
        for (int j = 0; j < nkeys; j++) { if (curr[j] == kc) { still = true; break; } }
        if (!still) {
            uint32_t lv = hidHostMapKeycode(0, kc);
            if (lv) { HidHostKeyEvt e{lv, false}; xQueueSend(hid_host_key_queue, &e, 0); }
        }
    }
    for (int i = 0; i < nkeys; i++) {
        uint8_t kc = curr[i];
        if (kc == 0) continue;
        bool had = false;
        for (int j = 0; j < 6; j++) { if (hid_host_prev_keys[j] == kc) { had = true; break; } }
        if (!had) {
            uint32_t lv = hidHostMapKeycode(mod, kc);
            if (lv) { HidHostKeyEvt e{lv, true}; xQueueSend(hid_host_key_queue, &e, 0); }
        }
    }
    std::memcpy(hid_host_prev_keys, curr, nkeys);
    if (nkeys < 6) std::memset(hid_host_prev_keys + nkeys, 0, 6 - nkeys);
}

static void hidHostMouseReadCb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    int32_t cx = hid_host_mouse_x.load();
    int32_t cy = hid_host_mouse_y.load();

    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        int32_t ow = lv_display_get_original_horizontal_resolution(disp);
        int32_t oh = lv_display_get_original_vertical_resolution(disp);
        switch (lv_display_get_rotation(disp)) {
            case LV_DISPLAY_ROTATION_0:
                data->point.x = (lv_coord_t)cx;
                data->point.y = (lv_coord_t)cy;
                break;
            case LV_DISPLAY_ROTATION_90:
                data->point.x = (lv_coord_t)cy;
                data->point.y = (lv_coord_t)(oh - cx - 1);
                break;
            case LV_DISPLAY_ROTATION_180:
                data->point.x = (lv_coord_t)(ow - cx - 1);
                data->point.y = (lv_coord_t)(oh - cy - 1);
                break;
            case LV_DISPLAY_ROTATION_270:
                data->point.x = (lv_coord_t)(ow - cy - 1);
                data->point.y = (lv_coord_t)cx;
                break;
        }
    } else {
        data->point.x = (lv_coord_t)cx;
        data->point.y = (lv_coord_t)cy;
    }
    data->state = hid_host_mouse_btn.load() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (!hid_host_mouse_active.load()) {
        hid_host_mouse_active.store(true);
        if (hid_host_ctx && hid_host_ctx->mouseCursor) {
            lv_obj_remove_flag(hid_host_ctx->mouseCursor, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hidHostHandleMouseReport(const uint8_t* data, uint16_t len) {
    if (len < 3) return;
    bool btn  = (data[0] & 0x01) != 0;
    int8_t dx = (int8_t)data[1];
    int8_t dy = (int8_t)data[2];

    lv_display_t* disp = lv_display_get_default();
    int32_t w = disp ? lv_display_get_horizontal_resolution(disp) : 320;
    int32_t h = disp ? lv_display_get_vertical_resolution(disp)   : 240;

    int32_t nx = hid_host_mouse_x.load() + dx;
    int32_t ny = hid_host_mouse_y.load() + dy;
    if (nx < 0) nx = 0;
    if (nx >= w) nx = w - 1;
    if (ny < 0) ny = 0;
    if (ny >= h) ny = h - 1;

    hid_host_mouse_x.store(nx);
    hid_host_mouse_y.store(ny);
    hid_host_mouse_btn.store(btn);

    if (hid_host_ctx && hid_host_ctx->mouseIndev == nullptr) {
        getMainDispatcher().dispatch([] {
            if (!hid_host_ctx || hid_host_ctx->mouseIndev != nullptr) return;
            if (!tt::lvgl::lock(1000)) { LOGGER.warn("LVGL lock failed for mouse indev"); return; }
            auto* ms = lv_indev_create();
            lv_indev_set_type(ms, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(ms, hidHostMouseReadCb);
            auto* cur = lv_image_create(lv_layer_sys());
            lv_obj_remove_flag(cur, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(cur, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(cur, TT_ASSETS_UI_CURSOR);
            lv_indev_set_cursor(ms, cur);
            hid_host_ctx->mouseIndev  = ms;
            hid_host_ctx->mouseCursor = cur;
            tt::lvgl::unlock();
            LOGGER.info("Mouse indev registered");
        });
    }
}

// ---- Timer callback for post-encryption CCCD retry ----

static void hidEncRetryTimerCb(void* /*arg*/) {
    if (hid_host_ctx) {
        if (!hid_host_ctx->typeResolutionDone) {
            LOGGER.warn("Post-encryption delay — type resolution timed out, proceeding");
            hid_host_ctx->typeResolutionDone = true;
            hid_host_ctx->subscribeIdx = 0;
        } else {
            LOGGER.info("Post-encryption delay complete — starting CCCD subscriptions");
        }
        hidHostSubscribeNext(*hid_host_ctx);
    }
}

// ---- Report Map parsing ----

static void applyReportMapTypes(HidHostCtx& ctx) {
    const uint8_t* data = ctx.rptMap.data();
    size_t len = ctx.rptMap.size();

    uint16_t usagePage = 0, usage = 0;
    uint8_t reportId = 0;
    int depth = 0;
    HidReportType collType = HidReportType::Unknown;

    struct Entry { uint8_t id; HidReportType type; };
    std::vector<Entry> typeMap;
    std::vector<HidReportType> collOrder;
    bool collHadInput = false;

    size_t i = 0;
    while (i < len) {
        uint8_t prefix = data[i++];
        if (prefix == 0xFE) {
            if (i + 1 >= len) break;
            uint8_t lsz = data[i++]; i++; i += lsz; continue;
        }
        uint8_t bSize = prefix & 0x03;
        uint8_t bType = (prefix >> 2) & 0x03;
        uint8_t bTag  = (prefix >> 4) & 0x0F;
        uint8_t dataLen = (bSize == 3) ? 4 : bSize;
        if (i + dataLen > len) break;
        uint32_t value = 0;
        for (uint8_t j = 0; j < dataLen; j++) value |= (uint32_t)data[i++] << (8 * j);

        if (bType == 0) {
            if (bTag == 0xA) {
                if (depth == 0 && value == 0x01) {
                    if      (usagePage == 0x01 && usage == 0x06) collType = HidReportType::Keyboard;
                    else if (usagePage == 0x01 && usage == 0x02) collType = HidReportType::Mouse;
                    else if (usagePage == 0x0C)                  collType = HidReportType::Consumer;
                    else                                          collType = HidReportType::Unknown;
                    collHadInput = false;
                }
                depth++; usage = 0;
            } else if (bTag == 0xC) {
                if (depth > 0) depth--;
                if (depth == 0) { collType = HidReportType::Unknown; collHadInput = false; }
                usage = 0;
            } else if (bTag == 0x8) {
                if (depth > 0 && collType != HidReportType::Unknown) {
                    if (!collHadInput) { collOrder.push_back(collType); collHadInput = true; }
                    if (reportId != 0) {
                        bool found = false;
                        for (const auto& e : typeMap) { if (e.id == reportId) { found = true; break; } }
                        if (!found) typeMap.push_back({reportId, collType});
                    }
                }
                usage = 0;
            } else { usage = 0; }
        } else if (bType == 1) {
            if      (bTag == 0x0) usagePage = (uint16_t)value;
            else if (bTag == 0x8) reportId  = (uint8_t)value;
        } else if (bType == 2) {
            if (bTag == 0x0) usage = (uint16_t)value;
        }
    }

    bool anyNonZeroId = false;
    for (const auto& rpt : ctx.inputRpts) { if (rpt.reportId != 0) { anyNonZeroId = true; break; } }

    size_t zeroRptIdx = 0;
    for (auto& rpt : ctx.inputRpts) {
        if (anyNonZeroId) {
            for (const auto& e : typeMap) { if (e.id == rpt.reportId) { rpt.type = e.type; break; } }
        } else {
            if (zeroRptIdx < collOrder.size()) rpt.type = collOrder[zeroRptIdx];
            zeroRptIdx++;
        }
        LOGGER.info("Report val_handle={} reportId={} type={}", rpt.valHandle, rpt.reportId, (int)rpt.type);
    }
    ctx.rptMap.clear();
}

// ---- Report Reference read chain ----

static void hidHostStartRptRefRead(HidHostCtx& ctx) {
    while (ctx.rptRefReadIdx < (int)ctx.inputRpts.size() &&
           ctx.inputRpts[ctx.rptRefReadIdx].rptRefHandle == 0) {
        ctx.rptRefReadIdx++;
    }
    if (ctx.rptRefReadIdx >= (int)ctx.inputRpts.size()) {
        hidHostReadReportMap(ctx);
        return;
    }
    uint16_t handle = ctx.inputRpts[ctx.rptRefReadIdx].rptRefHandle;
    int rc = ble_gattc_read(ctx.connHandle, handle, [](uint16_t conn_handle,
                             const struct ble_gatt_error* error,
                             struct ble_gatt_attr* attr, void* /*arg*/) -> int {
        if (!hid_host_ctx) return 0;
        auto& ctx = *hid_host_ctx;
        if (conn_handle != ctx.connHandle) return 0;
        if (error->status == BLE_HS_EDONE) return 0;
        if (error->status == 0 && attr != nullptr) {
            if (OS_MBUF_PKTLEN(attr->om) >= 2 && ctx.rptRefReadIdx < (int)ctx.inputRpts.size()) {
                uint8_t rpt_ref[2] = {};
                os_mbuf_copydata(attr->om, 0, 2, rpt_ref);
                ctx.inputRpts[ctx.rptRefReadIdx].reportId = rpt_ref[0];
                LOGGER.info("Report[{}] val_handle={} reportId={}", ctx.rptRefReadIdx,
                            ctx.inputRpts[ctx.rptRefReadIdx].valHandle, rpt_ref[0]);
            }
        }
        ctx.rptRefReadIdx++;
        hidHostStartRptRefRead(ctx);
        return 0;
    }, nullptr);
    if (rc != 0) {
        LOGGER.warn("rptRef read[{}] failed rc={} — skipping", ctx.rptRefReadIdx, rc);
        ctx.rptRefReadIdx++;
        hidHostStartRptRefRead(ctx);
    }
}

// ---- Report Map read ----

static void hidHostReadReportMap(HidHostCtx& ctx) {
    if (ctx.rptMapHandle == 0) {
        LOGGER.info("No Report Map char — skipping type resolution");
        ctx.typeResolutionDone = true;
        ctx.subscribeIdx = 0;
        hidHostSubscribeNext(ctx);
        return;
    }
    int rc = ble_gattc_read_long(ctx.connHandle, ctx.rptMapHandle, 0,
        [](uint16_t conn_handle, const struct ble_gatt_error* error,
           struct ble_gatt_attr* attr, void* /*arg*/) -> int {
            if (!hid_host_ctx) return 0;
            auto& ctx = *hid_host_ctx;
            if (conn_handle != ctx.connHandle) return 0;
            if (error->status == 0 && attr != nullptr) {
                uint16_t chunk = OS_MBUF_PKTLEN(attr->om);
                size_t old_sz = ctx.rptMap.size();
                ctx.rptMap.resize(old_sz + chunk);
                os_mbuf_copydata(attr->om, 0, chunk, ctx.rptMap.data() + old_sz);
                return 0;
            }
            if (!ctx.rptMap.empty()) {
                LOGGER.info("Report map read ({} bytes)", ctx.rptMap.size());
                applyReportMapTypes(ctx);
            } else {
                LOGGER.warn("Report map read failed — types remain Unknown");
            }
            ctx.typeResolutionDone = true;
            ctx.subscribeIdx = 0;
            hidHostSubscribeNext(ctx);
            return 0;
        }, nullptr);
    if (rc != 0) {
        LOGGER.warn("Report map read_long failed rc={} — skipping", rc);
        ctx.typeResolutionDone = true;
        ctx.subscribeIdx = 0;
        hidHostSubscribeNext(ctx);
    }
}

// ---- CCCD subscription chain ----

static int hidHostCccdWriteCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                               struct ble_gatt_attr* /*attr*/, void* /*arg*/) {
    if (!hid_host_ctx) return 0;
    auto& ctx = *hid_host_ctx;
    if (conn_handle != ctx.connHandle) return 0;

    if (error->status != 0 && error->status != BLE_HS_EDONE) {
        if ((error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
             error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC))
            && !ctx.securityInitiated) {
            LOGGER.info("CCCD auth required — initiating security");
            ctx.securityInitiated = true;
            ble_gap_security_initiate(conn_handle);
            return 0;
        }
        if (error->status == BLE_HS_ETIMEOUT) {
            LOGGER.warn("CCCD write timed out for report[{}] — skipping", ctx.subscribeIdx);
            ctx.subscribeIdx++;
            hidHostSubscribeNext(ctx);
            return 0;
        }
        if (error->status == BLE_HS_ENOTCONN) {
            LOGGER.warn("CCCD write failed — not connected");
            return 0;
        }
        LOGGER.warn("CCCD write failed status={}", error->status);
    }
    ctx.subscribeIdx++;
    hidHostSubscribeNext(ctx);
    return 0;
}

static void hidHostSubscribeNext(HidHostCtx& ctx) {
    if (ctx.subscribeIdx >= (int)ctx.inputRpts.size()) {
        if (ctx.readyBlockFired) {
            LOGGER.info("Subscribe ready block already ran — ignoring duplicate");
            return;
        }
        ctx.readyBlockFired = true;
        LOGGER.info("All {} reports subscribed — ready", ctx.inputRpts.size());
        if (hid_enc_retry_timer) esp_timer_stop(hid_enc_retry_timer);

        if (!hid_host_key_queue) {
            hid_host_key_queue = xQueueCreate(HID_HOST_KEY_QUEUE_SIZE, sizeof(HidHostKeyEvt));
        }
        getMainDispatcher().dispatch([] {
            if (!hid_host_ctx || hid_host_ctx->kbIndev != nullptr) return;
            if (!tt::lvgl::lock(1000)) { LOGGER.warn("LVGL lock failed for kb indev"); return; }
            auto* kb = lv_indev_create();
            lv_indev_set_type(kb, LV_INDEV_TYPE_KEYPAD);
            lv_indev_set_read_cb(kb, hidHostKeyboardReadCb);
            hid_host_ctx->kbIndev = kb;
            tt::lvgl::hardware_keyboard_set_indev(kb);
            tt::lvgl::unlock();
            LOGGER.info("Keyboard indev registered");
        });

        auto peer_addr = ctx.peerAddr;
        getMainDispatcher().dispatch([peer_addr] {
            // Find name from cached scan results
            std::string name;
            {
                auto results = getScanResults();
                for (const auto& r : results) {
                    if (r.addr == peer_addr) { name = r.name; break; }
                }
            }
            settings::PairedDevice device;
            device.addr        = peer_addr;
            device.profileId   = BT_PROFILE_HID_HOST;
            device.autoConnect = true;
            const auto addr_hex = settings::addrToHex(peer_addr);
            settings::PairedDevice existing;
            if (settings::load(addr_hex, existing)) {
                device.autoConnect = existing.autoConnect;
            }
            device.name = name;
            settings::save(device);
            if (struct Device* dev = findFirstDevice()) {
                struct BtEvent e = {};
                e.type = BT_EVENT_PROFILE_STATE_CHANGED;
                e.profile_state.state = BT_PROFILE_STATE_CONNECTED;
                e.profile_state.profile = BT_PROFILE_HID_HOST;
                bluetooth_fire_event(dev, e);
            }
        });
        return;
    }
    auto& rpt = ctx.inputRpts[ctx.subscribeIdx];
    if (rpt.cccdHandle == 0) {
        ctx.subscribeIdx++;
        hidHostSubscribeNext(ctx);
        return;
    }
    static const uint16_t notify_val = 0x0001;
    int rc = ble_gattc_write_flat(ctx.connHandle, rpt.cccdHandle,
                                   &notify_val, sizeof(notify_val),
                                   hidHostCccdWriteCb, nullptr);
    if (rc != 0) {
        LOGGER.warn("gattc_write_flat CCCD failed rc={}", rc);
        ctx.subscribeIdx++;
        hidHostSubscribeNext(ctx);
    }
}

// ---- Descriptor discovery ----

static int hidHostDscDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                             uint16_t chr_val_handle, const struct ble_gatt_dsc* dsc, void* /*arg*/) {
    if (!hid_host_ctx) return 0;
    auto& ctx = *hid_host_ctx;
    if (conn_handle != ctx.connHandle) return 0;

    if (error->status == 0 && dsc != nullptr) {
        uint16_t dsc_uuid = ble_uuid_u16(&dsc->uuid.u);
        for (auto& rpt : ctx.inputRpts) {
            if (rpt.valHandle != chr_val_handle) continue;
            if (dsc_uuid == 0x2902) { rpt.cccdHandle   = dsc->handle; }
            else if (dsc_uuid == 0x2908) { rpt.rptRefHandle = dsc->handle; }
            break;
        }
    } else if (error->status == BLE_HS_EDONE) {
        int next_idx = ctx.dscDiscIdx + 1;
        if (next_idx < (int)ctx.inputRpts.size()) {
            ctx.dscDiscIdx = next_idx;
            auto& next_rpt = ctx.inputRpts[next_idx];
            uint16_t end = getDescEndHandle(ctx, next_rpt.valHandle);
            int rc = ble_gattc_disc_all_dscs(ctx.connHandle, next_rpt.valHandle, end,
                                              hidHostDscDiscCb, nullptr);
            if (rc != 0) {
                LOGGER.warn("disc_all_dscs[{}] failed rc={}", next_idx, rc);
                ctx.rptRefReadIdx = 0;
                hidHostStartRptRefRead(ctx);
            }
        } else {
            ctx.rptRefReadIdx = 0;
            hidHostStartRptRefRead(ctx);
        }
    }
    return 0;
}

static uint16_t getDescEndHandle(const HidHostCtx& ctx, uint16_t valHandle) {
    for (uint16_t dh : ctx.allChrDefHandles) {
        if (dh > valHandle) return dh - 1;
    }
    return ctx.hidSvcEnd;
}

// ---- Characteristic discovery ----

static int hidHostChrDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                             const struct ble_gatt_chr* chr, void* /*arg*/) {
    if (!hid_host_ctx) return 0;
    auto& ctx = *hid_host_ctx;
    if (conn_handle != ctx.connHandle) return 0;

    if (error->status == 0 && chr != nullptr) {
        ctx.allChrDefHandles.push_back(chr->def_handle);
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
        if (uuid16 == 0x2A4D && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
            HidHostInputRpt rpt = {};
            rpt.valHandle = chr->val_handle;
            ctx.inputRpts.push_back(rpt);
            LOGGER.info("Input Report chr val_handle={}", chr->val_handle);
        } else if (uuid16 == 0x2A4B) {
            ctx.rptMapHandle = chr->val_handle;
        }
    } else if (error->status == BLE_HS_EDONE) {
        std::sort(ctx.allChrDefHandles.begin(), ctx.allChrDefHandles.end());
        if (ctx.inputRpts.empty()) {
            LOGGER.warn("No Input Report chars — disconnecting");
            ble_gap_terminate(ctx.connHandle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        ctx.dscDiscIdx = 0;
        auto& first = ctx.inputRpts[0];
        uint16_t end = getDescEndHandle(ctx, first.valHandle);
        int rc = ble_gattc_disc_all_dscs(ctx.connHandle, first.valHandle, end,
                                          hidHostDscDiscCb, nullptr);
        if (rc != 0) {
            LOGGER.warn("disc_all_dscs[0] failed rc={}", rc);
            ctx.rptRefReadIdx = 0;
            hidHostStartRptRefRead(ctx);
        }
    }
    return 0;
}

// ---- Service discovery ----

static int hidHostSvcDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                             const struct ble_gatt_svc* svc, void* /*arg*/) {
    if (!hid_host_ctx) return 0;
    auto& ctx = *hid_host_ctx;
    if (conn_handle != ctx.connHandle) return 0;

    if (error->status == 0 && svc != nullptr) {
        if (ble_uuid_u16(&svc->uuid.u) == 0x1812) {
            ctx.hidSvcStart = svc->start_handle;
            ctx.hidSvcEnd   = svc->end_handle;
            LOGGER.info("HID service start={} end={}", ctx.hidSvcStart, ctx.hidSvcEnd);
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (ctx.hidSvcStart == 0) {
            LOGGER.warn("No HID service found — disconnecting");
            ble_gap_terminate(ctx.connHandle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        int rc = ble_gattc_disc_all_chrs(ctx.connHandle, ctx.hidSvcStart, ctx.hidSvcEnd,
                                          hidHostChrDiscCb, nullptr);
        if (rc != 0) {
            LOGGER.warn("disc_all_chrs failed rc={}", rc);
            ble_gap_terminate(ctx.connHandle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    return 0;
}

// ---- GAP callback for HID host central connection ----

static int hidHostGapCb(struct ble_gap_event* event, void* /*arg*/) {
    if (!hid_host_ctx) return 0;
    auto& ctx = *hid_host_ctx;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ctx.connHandle = event->connect.conn_handle;
                LOGGER.info("Connected (handle={})", ctx.connHandle);
                int rc = ble_gattc_disc_all_svcs(ctx.connHandle, hidHostSvcDiscCb, nullptr);
                if (rc != 0) {
                    LOGGER.warn("disc_all_svcs failed rc={}", rc);
                    ble_gap_terminate(ctx.connHandle, BLE_ERR_REM_USER_CONN_TERM);
                }
            } else {
                LOGGER.warn("Connect failed status={}", event->connect.status);
                hid_host_ctx.reset();
                if (struct Device* dev = findFirstDevice()) {
                    bluetooth_set_hid_host_active(dev, false);
                    struct BtEvent e = {};
                    e.type = BT_EVENT_PROFILE_STATE_CHANGED;
                    e.profile_state.state = BT_PROFILE_STATE_IDLE;
                    e.profile_state.profile = BT_PROFILE_HID_HOST;
                    bluetooth_fire_event(dev, e);
                }
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            LOGGER.info("Disconnected reason={}", event->disconnect.reason);
            lv_indev_t* saved_kb     = hid_host_ctx ? hid_host_ctx->kbIndev     : nullptr;
            lv_indev_t* saved_mouse  = hid_host_ctx ? hid_host_ctx->mouseIndev  : nullptr;
            lv_obj_t*   saved_cursor = hid_host_ctx ? hid_host_ctx->mouseCursor : nullptr;
            QueueHandle_t saved_queue = hid_host_key_queue;
            hid_host_ctx.reset();
            hid_host_key_queue = nullptr;
            std::memset(hid_host_prev_keys, 0, sizeof(hid_host_prev_keys));
            hid_host_mouse_x.store(0);
            hid_host_mouse_y.store(0);
            hid_host_mouse_btn.store(false);
            hid_host_mouse_active.store(false);

            if (struct Device* dev = findFirstDevice()) {
                bluetooth_set_hid_host_active(dev, false);
                struct BtEvent e = {};
                e.type = BT_EVENT_PROFILE_STATE_CHANGED;
                e.profile_state.state = BT_PROFILE_STATE_IDLE;
                e.profile_state.profile = BT_PROFILE_HID_HOST;
                bluetooth_fire_event(dev, e);
            }

            getMainDispatcher().dispatch([saved_kb, saved_mouse, saved_cursor, saved_queue] {
                if (!tt::lvgl::lock(1000)) {
                    LOGGER.warn("Failed to acquire LVGL lock for indev cleanup");
                    if (saved_queue) vQueueDelete(saved_queue);
                    return;
                }
                if (saved_kb) {
                    tt::lvgl::hardware_keyboard_set_indev(nullptr);
                    lv_indev_delete(saved_kb);
                }
                if (saved_mouse)  lv_indev_delete(saved_mouse);
                if (saved_cursor) lv_obj_delete(saved_cursor);
                tt::lvgl::unlock();
                if (saved_queue) vQueueDelete(saved_queue);
            });
            break;
        }

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.conn_handle == ctx.connHandle) {
                if (event->enc_change.status == 0) {
                    LOGGER.info("Encryption established — retrying CCCD in 500ms");
                    ctx.subscribeIdx = 0;
                    if (hid_enc_retry_timer) {
                        esp_timer_stop(hid_enc_retry_timer);
                        esp_timer_start_once(hid_enc_retry_timer, 500 * 1000);
                    } else {
                        hidHostSubscribeNext(ctx);
                    }
                } else {
                    LOGGER.warn("Encryption failed status={}", event->enc_change.status);
                }
            }
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            if (event->notify_rx.conn_handle == ctx.connHandle) {
                uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
                if (len > 0 && len <= 64) {
                    uint8_t buf[64] = {};
                    os_mbuf_copydata(event->notify_rx.om, 0, len, buf);
                    for (const auto& rpt : ctx.inputRpts) {
                        if (rpt.valHandle != event->notify_rx.attr_handle) continue;
                        switch (rpt.type) {
                            case HidReportType::Keyboard:  hidHostHandleKeyboardReport(buf, len); break;
                            case HidReportType::Mouse:     hidHostHandleMouseReport(buf, len); break;
                            case HidReportType::Consumer:
                                LOGGER.info("Consumer report len={}", len);
                                break;
                            case HidReportType::Unknown:
                                if      (len >= 6) hidHostHandleKeyboardReport(buf, len);
                                else if (len >= 3) hidHostHandleMouseReport(buf, len);
                                break;
                        }
                        break;
                    }
                }
            }
            break;

        default:
            break;
    }
    return 0;
}

// ---- Public functions ----

void hidHostConnect(const std::array<uint8_t, 6>& addr) {
    if (getRadioState() != RadioState::On) {
        LOGGER.warn("hidHostConnect: radio not on");
        return;
    }
    if (hid_host_ctx) {
        LOGGER.warn("hidHostConnect: already connecting/connected");
        return;
    }

    hid_host_mouse_x.store(0);
    hid_host_mouse_y.store(0);
    hid_host_mouse_btn.store(false);
    hid_host_mouse_active.store(false);

    hid_host_ctx = std::make_unique<HidHostCtx>();
    hid_host_ctx->peerAddr = addr;

    // Create enc retry timer lazily
    if (hid_enc_retry_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback        = hidEncRetryTimerCb;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name            = "hid_enc_retry";
        if (esp_timer_create(&args, &hid_enc_retry_timer) != ESP_OK) {
            LOGGER.error("Failed to create hid_enc_retry timer");
            hid_enc_retry_timer = nullptr;
        }
    }

    // Notify driver that a HID host central connection is starting.
    if (struct Device* dev = findFirstDevice()) bluetooth_set_hid_host_active(dev, true);

    // Look up the addr_type from the cached scan results.
    ble_addr_t ble_addr = {};
    ble_addr.type = BLE_ADDR_PUBLIC;
    std::memcpy(ble_addr.val, addr.data(), 6);
    uint8_t addr_type = 0;
    if (getCachedScanAddrType(addr.data(), &addr_type)) {
        ble_addr.type = addr_type;
    }

    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    int rc = ble_gap_connect(own_addr_type, &ble_addr, 5000, nullptr, hidHostGapCb, nullptr);
    if (rc != 0) {
        LOGGER.warn("ble_gap_connect failed rc={}", rc);
        hid_host_ctx.reset();
        if (struct Device* dev = findFirstDevice()) {
            bluetooth_set_hid_host_active(dev, false);
            // Fire IDLE so bt_event_bridge can start a new scan and retry.
            struct BtEvent e = {};
            e.type = BT_EVENT_PROFILE_STATE_CHANGED;
            e.profile_state.state   = BT_PROFILE_STATE_IDLE;
            e.profile_state.profile = BT_PROFILE_HID_HOST;
            bluetooth_fire_event(dev, e);
        }
    } else {
        LOGGER.info("Connecting...");
    }
}

void hidHostDisconnect() {
    if (!hid_host_ctx || hid_host_ctx->connHandle == BLE_HS_CONN_HANDLE_NONE) return;
    ble_gap_terminate(hid_host_ctx->connHandle, BLE_ERR_REM_USER_CONN_TERM);
}

bool hidHostIsConnected() {
    return hid_host_ctx != nullptr &&
           hid_host_ctx->connHandle != BLE_HS_CONN_HANDLE_NONE &&
           !hid_host_ctx->inputRpts.empty() &&
           hid_host_ctx->subscribeIdx >= (int)hid_host_ctx->inputRpts.size();
}

bool hidHostGetConnectedPeer(std::array<uint8_t, 6>& addr_out) {
    if (!hidHostIsConnected()) return false;
    addr_out = hid_host_ctx->peerAddr;
    return true;
}

void autoConnectHidHost() {
    if (hidHostIsConnected()) return;

    // Connect to the first saved HID host peer that appeared in the last scan.
    // cacheScanAddr() is populated during scanning so addr_type is available for ble_gap_connect.
    auto scan = getScanResults();
    for (const auto& r : scan) {
        settings::PairedDevice stored;
        if (settings::load(settings::addrToHex(r.addr), stored) &&
            stored.autoConnect &&
            stored.profileId == BT_PROFILE_HID_HOST) {
            LOGGER.info("Auto-connecting HID host to {}", settings::addrToHex(r.addr));
            hidHostConnect(r.addr);
            return;
        }
    }

    // Device not in the last scan. If we have an autoConnect HID host peer, restart
    // scanning so we keep checking until the device powers back on.
    auto peers = settings::loadAll();
    for (const auto& peer : peers) {
        if (peer.autoConnect && peer.profileId == BT_PROFILE_HID_HOST) {
            if (struct Device* dev = findFirstDevice()) {
                if (!bluetooth_is_scanning(dev)) {
                    LOGGER.info("Auto-connect HID host: device not in scan, retrying scan");
                    bluetooth_scan_start(dev);
                }
            }
            break;
        }
    }
}

} // namespace tt::bluetooth

#endif // CONFIG_BT_NIMBLE_ENABLED
