#include <format>
#include <string>

#include <lvgl/lvgl.h>

#include <Tactility/app/btmanage/View.h>
#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btpeersettings/BtPeerSettings.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/bluetooth/BluetoothSettings.h>
#include <Tactility/bluetooth/BluetoothPairedDevice.h>
#include <Tactility/Tactility.h>

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

static void onEnableOnBootSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    getMainDispatcher().dispatch([is_on] {
        bluetooth::settings::setEnableOnBoot(is_on);
    });
}

static void onEnableOnBootParentClicked(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    bool new_state = !lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    if (new_state) {
        lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
    }
    // add/remove_state does not fire LV_EVENT_VALUE_CHANGED, so persist here directly.
    getMainDispatcher().dispatch([new_state] {
        bluetooth::settings::setEnableOnBoot(new_state);
    });
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
            btpeersettings::start(bluetooth::settings::addrToHex(peers[data->index].addr));
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

void View::createPeerListItem(lv_obj_t* list, const bluetooth::PeerRecord& record, bool isPaired, size_t index) {
    const auto percentage = mapRssiToPercentage(record.rssi);
    const auto label = record.name.empty()
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
        using enum bluetooth::RadioState;
        case On:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            break;
        case OnPending:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
        case Off:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_remove_state(enable_switch, LV_STATE_DISABLED);
            break;
        case OffPending:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
    }
}

void View::updateEnableOnBootToggle() {
    if (enable_on_boot_switch != nullptr) {
        lv_obj_clear_state(enable_on_boot_switch, LV_STATE_ANY);
        if (bluetooth::settings::shouldEnableOnBoot()) {
            lv_obj_add_state(enable_on_boot_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enable_on_boot_switch, LV_STATE_CHECKED);
        }
    }
}

void View::updateScanning() {
    if (state->getRadioState() == bluetooth::RadioState::On && state->isScanning()) {
        lv_obj_set_hidden(scanning_spinner, false);
    } else {
        lv_obj_set_hidden(scanning_spinner, true);
    }
}

void View::updatePeerList() {
    lv_obj_clean(peers_list);

    // Enable on boot

    auto* enable_on_boot_wrapper = lv_obj_create(peers_list);
    lv_obj_set_size(enable_on_boot_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(enable_on_boot_wrapper, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(enable_on_boot_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_on_boot_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(enable_on_boot_wrapper, false);

    auto* enable_label = lv_label_create(enable_on_boot_wrapper);
    lv_label_set_text(enable_label, "Enable on boot");
    lv_obj_set_flex_grow(enable_label, 1);

    enable_on_boot_switch = lv_switch_create(enable_on_boot_wrapper);
    lv_obj_add_event_cb(enable_on_boot_switch, onEnableOnBootSwitchChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(enable_on_boot_wrapper, onEnableOnBootParentClicked, LV_EVENT_SHORT_CLICKED, enable_on_boot_switch);

    updateEnableOnBootToggle();

    using enum bluetooth::RadioState;
    if (state->getRadioState() == On) {
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
        if (!scan_results.empty()) {
            for (size_t i = 0; i < scan_results.size(); ++i) {
                createPeerListItem(available_section, scan_results[i], false, i);
            }
        } else if (!state->isScanning()) {
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

void View::update() {
    updateBtToggle();
    updateScanning();
    updatePeerList();
}

} // namespace tt::app::btmanage
