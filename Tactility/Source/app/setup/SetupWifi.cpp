#include <Tactility/app/setup/SetupPrivate.h>
#include <Tactility/app/wificonnect/WifiConnect.h>

#include <wifi/wifi_settings.h>

#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl.h>

#include <set>

namespace tt::app::setup {

constexpr auto* TAG = "setup";

static void connectToNetwork(Context* ctx, const std::string& ssid) {
    ctx->wifiTarget = ssid;
    ctx->wifiConnectFailed = false;
    ctx->connectRequest = ssid;
    task_event_group_signal(ctx->eventGroup, ctx->wifiActionBit);
}

static void onWifiNetworkClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
    if (index >= ctx->wifiSsids.size()) {
        return;
    }
    const std::string ssid = ctx->wifiSsids[index];
    if (ctx->wifiState.getStationState() == WIFI_STATION_STATE_CONNECTED && ctx->wifiState.getConnectionTarget() == ssid) {
        return;
    }
    connectToNetwork(ctx, ssid);
}

static void onWifiRetryClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    connectToNetwork(ctx, ctx->wifiTarget);
}

/** A text with a spinner next to it */
static void createBusyRow(lv_obj_t* parent, const char* text) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, LV_DPX(8), LV_STATE_DEFAULT);
    lv_label_set_text(lv_label_create(row), text);
    lvgl_spinner_create(row);
}

/** Shows the network list, or the connection progress or failure in its place */
void renderWifi(Context* ctx) {
    auto* container = ctx->wifiContainer;
    lv_obj_clean(container);
    ctx->wifiSsids.clear();

    const auto station = ctx->wifiState.getStationState();
    lv_obj_set_state(ctx->continueButton, LV_STATE_DISABLED, station != WIFI_STATION_STATE_CONNECTED);

    if (station == WIFI_STATION_STATE_CONNECTION_PENDING) {
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        createBusyRow(container, "Connecting");
        return;
    }

    if (ctx->wifiConnectFailed) {
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(container, LV_DPX(8), LV_STATE_DEFAULT);
        lv_label_set_text(lv_label_create(container), "Failed to connect");
        auto* retry = lv_button_create(container);
        lv_label_set_text(lv_label_create(retry), "Retry");
        lv_obj_add_event_cb(retry, onWifiRetryClicked, LV_EVENT_SHORT_CLICKED, ctx);
        return;
    }

    const auto records = ctx->wifiState.getApRecords();
    if (records.empty()) {
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (ctx->wifiDevice == nullptr) {
            lv_label_set_text(lv_label_create(container), "No Wi-Fi available.");
        } else if (ctx->wifiState.isScanning() || !ctx->wifiState.hasScannedAfterRadioOn()) {
            createBusyRow(container, "Scanning");
        } else {
            lv_label_set_text(lv_label_create(container), "No networks found.");
        }
        return;
    }

    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    auto* card = lvgl_card_create(container);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
    // The list items' pressed and focused backgrounds follow the card's rounded corners
    lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);
    auto* list = lvgl_list_create(card);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);

    std::set<std::string> used_ssids;
    for (const auto& record : records) {
        std::string ssid = record.ssid;
        if (ssid.empty() || !used_ssids.insert(ssid).second) {
            continue;
        }
        const bool isConnected = station == WIFI_STATION_STATE_CONNECTED && ssid == ctx->wifiState.getConnectionTarget();
        const std::string text = isConnected ? ssid + " (connected)" : ssid;
        auto* button = lvgl_list_add_button(list, isConnected ? LV_SYMBOL_OK : LV_SYMBOL_WIFI, text.c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(ctx->wifiSsids.size()));
        lv_obj_add_event_cb(button, onWifiNetworkClicked, LV_EVENT_SHORT_CLICKED, ctx);
        ctx->wifiSsids.push_back(std::move(ssid));
    }
    lvgl_list_hide_last_divider(card);
}

static void updateWifiState(Context* ctx) {
    if (ctx->wifiDevice == nullptr) {
        return;
    }
    WifiRadioState radio = WIFI_RADIO_STATE_OFF;
    WifiStationState station = WIFI_STATION_STATE_DISCONNECTED;
    char ssid[33] = {};
    wifi_get_radio_state(ctx->wifiDevice, &radio);
    wifi_get_station_state(ctx->wifiDevice, &station);
    if (station != WIFI_STATION_STATE_DISCONNECTED) {
        wifi_station_get_target_ssid(ctx->wifiDevice, ssid);
    }
    ctx->wifiState.setRadioState(radio);
    ctx->wifiState.setStationState(station);
    ctx->wifiState.setConnectionTarget(ssid);
    if (station == WIFI_STATION_STATE_CONNECTION_PENDING || station == WIFI_STATION_STATE_CONNECTED) {
        ctx->wifiConnectFailed = false;
        if (ssid[0] != '\0') {
            ctx->wifiTarget = ssid;
        }
    }
}

void enterWifiSetup(Context* ctx) {
    lvgl_lock();
    ctx->radioOnRequested = true;
    lvgl_unlock();
    task_event_group_signal(ctx->eventGroup, ctx->wifiActionBit);

    lvgl_lock();
    ctx->phase = Phase::WifiSetup;
    ctx->wifiConnectFailed = false;
    updateWifiState(ctx);
    if (ctx->wifiDevice != nullptr) {
        ctx->wifiState.setScanning(wifi_is_scanning(ctx->wifiDevice));
        ctx->wifiState.updateApRecords(ctx->wifiDevice);
    }
    if (ctx->titleLabel != nullptr) {
        renderCurrent(ctx);
    }
    lvgl_unlock();
}

/** Turns the radio on and connects as requested by the UI, on the app's task as both block */
static void processWifiActions(Context* ctx) {
    lvgl_lock();
    const bool radioOn = ctx->radioOnRequested;
    ctx->radioOnRequested = false;
    const std::string ssid = std::move(ctx->connectRequest);
    ctx->connectRequest.clear();
    lvgl_unlock();

    if (ctx->wifiDevice == nullptr) {
        return;
    }
    if (radioOn) {
        if (wifi_set_radio_on(ctx->wifiDevice) == ERROR_NONE) {
            if (wifi_settings_set_enable(true) != ERROR_NONE) {
                LOG_E(TAG, "Failed to save the Wi-Fi state");
            }
            if (!wifi_is_scanning(ctx->wifiDevice) && wifi_scan(ctx->wifiDevice) != ERROR_NONE) {
                LOG_E(TAG, "Failed to start scan");
            }
        } else {
            LOG_E(TAG, "Failed to turn the Wi-Fi radio on");
        }
    }
    if (!ssid.empty()) {
        WifiApSettings ap;
        if (wifi_settings_load(ssid.c_str(), &ap) != ERROR_NONE) {
            wificonnect::start(ssid, "", true);
        } else {
            const error_t result = wifi_station_connect(ctx->wifiDevice, ap.ssid, ap.password, ap.channel);
            if (result != ERROR_NONE) {
                LOG_E(TAG, "Failed to connect (%s)", error_to_string(result));
            }
        }
    }
}

static void onWifiEvent(Context* ctx, WifiEvent event) {
    lvgl_lock();
    updateWifiState(ctx);
    bool rebuild = true;
    switch (event.type) {
        case WIFI_EVENT_TYPE_SCAN_STARTED:
            ctx->wifiState.setScanning(true);
            // Rebuilding the list resets the keyboard focus
            rebuild = ctx->wifiState.getApRecords().empty();
            break;
        case WIFI_EVENT_TYPE_SCAN_FINISHED:
            ctx->wifiState.setScanning(false);
            ctx->wifiState.updateApRecords(ctx->wifiDevice);
            break;
        case WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT:
            ctx->wifiConnectFailed = event.connection_error != WIFI_STATION_CONNECTION_ERROR_NONE;
            break;
        default:
            break;
    }
    // Covered by another app: createWidgets() renders the current page when the window is shown again
    if (rebuild && ctx->phase == Phase::WifiSetup && ctx->wifiContainer != nullptr) {
        renderWifi(ctx);
    }
    lvgl_unlock();
}

void wifiStart(Context* ctx, TaskEventGroup* eventGroup) {
    ctx->eventGroup = eventGroup;
    if (task_event_group_claim_bit(eventGroup, &ctx->wifiActionBit) != ERROR_NONE) {
        LOG_W(TAG, "Failed to claim a Wi-Fi action bit");
    }

    Device* wifi_device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) == ERROR_NONE) {
        if (wifi_event_subscribe(wifi_device, &ctx->wifiEventSub, eventGroup) == ERROR_NONE) {
            ctx->wifiDevice = wifi_device;
        } else {
            LOG_W(TAG, "Failed to subscribe to Wi-Fi events");
            device_put(wifi_device);
        }
    }
}

void wifiStop(Context* ctx) {
    if (ctx->wifiDevice != nullptr) {
        wifi_event_unsubscribe(ctx->wifiDevice, &ctx->wifiEventSub);
        device_put(ctx->wifiDevice);
        ctx->wifiDevice = nullptr;
    }
}

void wifiProcess(Context* ctx) {
    processWifiActions(ctx);

    if (ctx->wifiDevice != nullptr) {
        WifiEvent wifi_event {};
        while (wifi_event_poll(&ctx->wifiEventSub, &wifi_event) == ERROR_NONE) {
            onWifiEvent(ctx, wifi_event);
        }
    }
}

}
