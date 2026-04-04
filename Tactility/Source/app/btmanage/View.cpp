#include <format>
#include <string>

#include <tactility/lvgl_module.h>

#include <Tactility/app/btmanage/View.h>
#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btpeersettings/BtPeerSettings.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/bluetooth/BluetoothSettings.h>
#include <Tactility/bluetooth/BluetoothPairedDevice.h>
#include <Tactility/Tactility.h>

namespace tt::app::btmanage {

static const auto LOGGER = Logger("BtManageView");

static void onEnableSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    auto bt = std::static_pointer_cast<BtManage>(getCurrentApp());
    bt->getBindings().onBtToggled(is_on);
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
    auto bt = std::static_pointer_cast<BtManage>(getCurrentApp());
    struct Device* dev = bluetooth::findFirstDevice();
    bool scanning = dev ? bluetooth_is_scanning(dev) : false;
    bt->getBindings().onScanToggled(!scanning);
}

// region Peer list callbacks

struct PeerListItemData {
    View* view;
    size_t index;
    bool isPaired;
};

void View::onConnect(lv_event_t* event) {
    auto* data = static_cast<PeerListItemData*>(lv_event_get_user_data(event));
    auto bt = std::static_pointer_cast<BtManage>(getCurrentApp());
    auto& state = bt->getState();

    if (data->isPaired) {
        // Open the per-device settings screen for paired devices
        auto peers = state.getPairedPeers();
        if (data->index < peers.size()) {
            btpeersettings::start(bluetooth::settings::addrToHex(peers[data->index].addr));
        }
    } else {
        // Unrecognised scan result — initiate pairing
        auto peers = state.getScanResults();
        if (data->index < peers.size()) {
            bt->getBindings().onPairPeer(peers[data->index].addr);
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

void View::createPeerListItem(const bluetooth::PeerRecord& record, bool isPaired, size_t index) {
    const auto percentage = mapRssiToPercentage(record.rssi);
    const auto label = record.name.empty()
        ? std::format("Unknown ({:02x}{:02x}{:02x}{:02x}{:02x}{:02x}) {}%",
            record.addr[0], record.addr[1], record.addr[2],
            record.addr[3], record.addr[4], record.addr[5],
            percentage)
        : std::format("{} {}%", record.name, percentage);

    auto* button = lv_list_add_button(peers_list, nullptr, label.c_str());

    auto* item_data = new PeerListItemData { this, index, isPaired };
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
        lv_obj_remove_flag(scanning_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(scanning_spinner, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::updatePeerList() {
    lv_obj_clean(peers_list);

    // Enable on boot row
    auto* enable_on_boot_wrapper = lv_obj_create(peers_list);
    lv_obj_set_size(enable_on_boot_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);

    auto* enable_label = lv_label_create(enable_on_boot_wrapper);
    lv_label_set_text(enable_label, "Enable on boot");
    lv_obj_align(enable_label, LV_ALIGN_LEFT_MID, 0, 0);

    enable_on_boot_switch = lv_switch_create(enable_on_boot_wrapper);
    lv_obj_align(enable_on_boot_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(enable_on_boot_switch, onEnableOnBootSwitchChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(enable_on_boot_wrapper, onEnableOnBootParentClicked, LV_EVENT_SHORT_CLICKED, enable_on_boot_switch);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_ver(enable_on_boot_wrapper, 2, LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_pad_ver(enable_on_boot_wrapper, 8, LV_STATE_DEFAULT);
    }

    updateEnableOnBootToggle();

    using enum bluetooth::RadioState;
    if (state->getRadioState() == On) {
        // Paired peers section
        auto paired = state->getPairedPeers();
        if (!paired.empty()) {
            lv_list_add_text(peers_list, "Paired");
            for (size_t i = 0; i < paired.size(); ++i) {
                createPeerListItem(paired[i], true, i);
            }
        }

        // Scan results section
        auto scan_results = state->getScanResults();
        lv_list_add_text(peers_list, "Available");
        if (!scan_results.empty()) {
            for (size_t i = 0; i < scan_results.size(); ++i) {
                createPeerListItem(scan_results[i], false, i);
            }
        } else if (!state->isScanning()) {
            auto* no_devices_label = lv_label_create(peers_list);
            lv_label_set_text(no_devices_label, "No devices found.");
        }
        // Never hide peers_list: it always contains the "Enable on boot" row.
        // While scanning with no results the spinner in the toolbar provides feedback.

        // Scan button
        auto* scan_button = lv_button_create(peers_list);
        lv_obj_set_width(scan_button, LV_PCT(100));
        lv_obj_set_style_margin_ver(scan_button, 4, LV_STATE_DEFAULT);
        auto* scan_label = lv_label_create(scan_button);
        lv_label_set_text(scan_label, state->isScanning() ? "Stop scan" : "Scan");
        lv_obj_add_event_cb(scan_button, onScanButtonClicked, LV_EVENT_SHORT_CLICKED, nullptr);
    }
}

// endregion Secondary updates

void View::init(const AppContext& app, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    root = parent;
    paths = app.getPaths();

    // Toolbar
    auto* toolbar = lvgl::toolbar_create(parent, app);

    scanning_spinner = lvgl::toolbar_add_spinner_action(toolbar);

    enable_switch = lvgl::toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(enable_switch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    // Peer list
    peers_list = lv_list_create(parent);
    lv_obj_set_flex_grow(peers_list, 1);
    lv_obj_set_width(peers_list, LV_PCT(100));
}

void View::update() {
    updateBtToggle();
    updateScanning();
    updatePeerList();
}

} // namespace tt::app::btmanage
