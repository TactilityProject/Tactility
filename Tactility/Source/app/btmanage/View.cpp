#include <format>
#include <string>

#include <lvgl/lvgl.h>

#include <Tactility/app/btmanage/View.h>
#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btpeersettings/BtPeerSettings.h>
#include <Tactility/Tactility.h>

#include <bluetooth/bluetooth_paired_device.h>

#include <app/event.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::btmanage {

static void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

static void onEnableSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->bindings.onBtToggled(ctx, is_on);
}

static void onScanButtonClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    Device* dev = nullptr;
    device_get_first_active_by_type(&BLUETOOTH_TYPE, &dev);
    bool scanning = dev ? bluetooth_is_scanning(dev) : false;
    if (dev) {
        device_put(dev);
    }
    ctx->bindings.onScanToggled(ctx, !scanning);
}

// region Peer list callbacks

struct PeerListItemData {
    void* context;
    State* state;
    Bindings* bindings;
    size_t index;
    bool isPaired;
};

void View::onConnect(lv_event_t* event) {
    auto* data = static_cast<PeerListItemData*>(lv_event_get_user_data(event));

    if (data->isPaired) {
        // Open the per-device settings screen for paired devices
        auto peers = data->state->getPairedPeers();
        if (data->index < peers.size()) {
            char addr_hex[BLUETOOTH_ADDR_HEX_LENGTH + 1];
            bluetooth_addr_to_hex(peers[data->index].addr, addr_hex);
            btpeersettings::start(addr_hex);
        }
    } else {
        // Unrecognised scan result — initiate pairing
        auto peers = data->state->getScanResults();
        if (data->index < peers.size()) {
            data->bindings->onPairPeer(data->context, peers[data->index].addr);
        }
    }
}

// endregion Peer list callbacks

static uint8_t mapRssiToPercentage(int8_t rssi) {
    auto abs_rssi = std::abs(rssi);
    if (abs_rssi < 30) abs_rssi = 30;
    if (abs_rssi > 90) abs_rssi = 90;
    return static_cast<uint8_t>((float)(90 - abs_rssi) / 60.f * 100.f);
}

/** Creates a title with a card below it, and returns the list in the card */
static lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, title);

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
    // The list items' pressed and focused backgrounds follow the card's rounded corners
    lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);

    // The card provides the background
    auto* list = lvgl_list_create(card);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);
    return list;
}

void View::createPeerListItem(lv_obj_t* list, const BluetoothPeer& record, bool isPaired, size_t index) {
    const auto percentage = mapRssiToPercentage(record.rssi);
    const auto label = record.name[0] == '\0'
        ? std::format("Unknown ({:02x}{:02x}{:02x}{:02x}{:02x}{:02x}) {}%",
            record.addr[0], record.addr[1], record.addr[2],
            record.addr[3], record.addr[4], record.addr[5],
            percentage)
        : std::format("{} {}%", record.name, percentage);

    auto* button = lvgl_list_add_button(list, nullptr, label.c_str());

    auto* item_data = new PeerListItemData { context, state, bindings, index, isPaired };
    lv_obj_set_user_data(button, item_data);
    lv_obj_add_event_cb(button, onConnect, LV_EVENT_SHORT_CLICKED, item_data);
    lv_obj_add_event_cb(button, [](lv_event_t* e) {
        delete static_cast<PeerListItemData*>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
    }, LV_EVENT_DELETE, nullptr);
}

// region Secondary updates

void View::updateBtToggle() {
    lv_obj_clear_state(enable_switch, LV_STATE_ANY);
    switch (state->getRadioState()) {
        case BT_RADIO_STATE_ON:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            break;
        case BT_RADIO_STATE_ON_PENDING:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
        case BT_RADIO_STATE_OFF:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_remove_state(enable_switch, LV_STATE_DISABLED);
            break;
        case BT_RADIO_STATE_OFF_PENDING:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
    }
}

void View::updateScanning() {
    if (state->getRadioState() == BT_RADIO_STATE_ON && state->isScanning()) {
        lv_obj_set_hidden(scanning_spinner, false);
    } else {
        lv_obj_set_hidden(scanning_spinner, true);
    }
}

void View::updatePeerList() {
    lv_obj_clean(peers_list);

    if (state->getRadioState() == BT_RADIO_STATE_ON) {
        // Paired peers section
        auto paired = state->getPairedPeers();
        if (!paired.empty()) {
            auto* paired_section = createSection(peers_list, "Paired");
            for (size_t i = 0; i < paired.size(); ++i) {
                createPeerListItem(paired_section, paired[i], true, i);
            }
        }

        // Scan results section
        auto scan_results = state->getScanResults();
        auto* available_section = createSection(peers_list, "Available");
        // Devices without a name are skipped. The index stays that of the scan results, which the click handler uses.
        bool hasNamedResults = false;
        for (size_t i = 0; i < scan_results.size(); ++i) {
            if (scan_results[i].name[0] != '\0') {
                createPeerListItem(available_section, scan_results[i], false, i);
                hasNamedResults = true;
            }
        }
        if (!hasNamedResults && !state->isScanning()) {
            lvgl_list_add_text(available_section, "No devices found.");
        }
        // Never hide peers_list: it always contains the "Enable on boot" row.
        // While scanning with no results the spinner in the toolbar provides feedback.

        // Scan button
        auto* scan_button = lv_button_create(peers_list);
        lv_obj_set_width(scan_button, LV_PCT(100));
        lv_obj_set_style_margin_ver(scan_button, 4, LV_STATE_DEFAULT);
        auto* scan_label = lv_label_create(scan_button);
        lv_label_set_text(scan_label, state->isScanning() ? "Stop scan" : "Scan");
        lv_obj_add_event_cb(scan_button, onScanButtonClicked, LV_EVENT_SHORT_CLICKED, context);
    }
}

// endregion Secondary updates

void View::init(void* newContext, lv_obj_t* parent) {
    context = newContext;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    root = parent;

    // Toolbar
    auto* toolbar = lvgl_toolbar_create(parent, "Bluetooth");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, context);

    scanning_spinner = lvgl_toolbar_add_spinner_action(toolbar);

    enable_switch = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(enable_switch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, context);

    // Peer list
    peers_list = lv_obj_create(parent);
    lv_obj_set_flex_grow(peers_list, 1);
    lv_obj_set_width(peers_list, LV_PCT(100));
    lv_obj_set_flex_flow(peers_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(peers_list, 0, LV_STATE_DEFAULT);
}

void View::destroyWidgets() {
    root = nullptr;
    enable_switch = nullptr;
    scanning_spinner = nullptr;
    peers_list = nullptr;
}

void View::update() {
    // The widgets are gone while another window covers this one
    if (root == nullptr) {
        return;
    }
    updateBtToggle();
    updateScanning();
    updatePeerList();
}

} // namespace tt::app::btmanage
