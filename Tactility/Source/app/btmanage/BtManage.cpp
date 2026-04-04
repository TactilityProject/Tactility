#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btmanage/View.h>

#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/Tactility.h>

#include <tactility/lvgl_icon_shared.h>

namespace tt::app::btmanage {

static const auto LOGGER = Logger("BtManage");

extern const AppManifest manifest;

static void onBtToggled(bool enabled) {
    struct Device* dev = bluetooth::findFirstDevice();
    if (!dev) return;
    bluetooth_set_radio_enabled(dev, enabled);
}

static void onScanToggled(bool enabled) {
    struct Device* dev = bluetooth::findFirstDevice();
    if (!dev) return;
    if (enabled) {
        bluetooth_scan_start(dev);
    } else {
        bluetooth_scan_stop(dev);
    }
}

static void onConnectPeer(const std::array<uint8_t, 6>& addr, int profileId) {
    bluetooth::connect(addr, profileId);
}

static void onDisconnectPeer(const std::array<uint8_t, 6>& addr, int profileId) {
    bluetooth::disconnect(addr, profileId);
}

static void onPairPeer(const std::array<uint8_t, 6>& addr) {
    // Clicking an unrecognised scan result initiates a HID host connection.
    // Bond exchange happens automatically during the first connection.
    bluetooth::hidHostConnect(addr);
}

static void onForgetPeer(const std::array<uint8_t, 6>& addr) {
    bluetooth::unpair(addr);
}

BtManage::BtManage() {
    bindings = (Bindings) {
        .onBtToggled = onBtToggled,
        .onScanToggled = onScanToggled,
        .onConnectPeer = onConnectPeer,
        .onDisconnectPeer = onDisconnectPeer,
        .onPairPeer = onPairPeer,
        .onForgetPeer = onForgetPeer,
    };
}

void BtManage::lock() {
    mutex.lock();
}

void BtManage::unlock() {
    mutex.unlock();
}

void BtManage::requestViewUpdate() {
    lock();
    if (isViewEnabled) {
        if (lvgl::lock(1000)) {
            view.update();
            lvgl::unlock();
        } else {
            LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
        }
    }
    unlock();
}

void BtManage::onBtEvent(const struct BtEvent& event) {
    auto radio_state = bluetooth::getRadioState();
    LOGGER.info("Update with state {}", bluetooth::radioStateToString(radio_state));
    getState().setRadioState(radio_state);
    switch (event.type) {
        case BT_EVENT_SCAN_STARTED:
            getState().setScanning(true);
            break;
        case BT_EVENT_SCAN_FINISHED:
            getState().setScanning(false);
            getState().updateScanResults();
            getState().updatePairedPeers();
            break;
        case BT_EVENT_PEER_FOUND:
            getState().updateScanResults();
            break;
        case BT_EVENT_PAIR_RESULT:
            getState().updatePairedPeers();
            break;
        case BT_EVENT_PROFILE_STATE_CHANGED:
            getState().updateScanResults();
            getState().updatePairedPeers();
            break;
        case BT_EVENT_RADIO_STATE_CHANGED:
            if (event.radio_state == BT_RADIO_STATE_ON) {
                getState().updatePairedPeers();
                struct Device* dev = bluetooth::findFirstDevice();
                if (dev && !bluetooth_is_scanning(dev)) {
                    bluetooth_scan_start(dev);
                }
            }
            break;
        default:
            break;
    }

    requestViewUpdate();
}

static void onKernelBtEvent(struct Device* /*device*/, void* context, struct BtEvent event) {
    // BT event callbacks can fire from the NimBLE host task (e.g. DISCONNECT during
    // nimble_port_stop shutdown). Calling onBtEvent() synchronously from the NimBLE
    // task would block it on the LVGL mutex (held by the LVGL task waiting in
    // nimble_port_stop), creating a permanent deadlock. Dispatch to the main task so
    // the NimBLE host task is never blocked by BtManage's state updates or LVGL lock.
    auto* self = static_cast<BtManage*>(context);
    getMainDispatcher().dispatch([self, event] {
        self->onBtEvent(event);
    });
}

void BtManage::onShow(AppContext& app, lv_obj_t* parent) {
    // Initialise state and view before subscribing to avoid incoming events
    // racing with state initialisation.
    state.setRadioState(bluetooth::getRadioState());
    struct Device* dev = bluetooth::findFirstDevice();
    state.setScanning(dev ? bluetooth_is_scanning(dev) : false);
    state.updateScanResults();
    state.updatePairedPeers();

    lock();
    isViewEnabled = true;
    view.init(app, parent);
    view.update();
    unlock();

    btDevice = dev;
    if (btDevice) {
        bluetooth_add_event_callback(btDevice, this, onKernelBtEvent);
    }

    auto radio_state = bluetooth::getRadioState();
    bool can_scan = radio_state == bluetooth::RadioState::On;
    LOGGER.info("Radio: {}, Scanning: {}, Can scan: {}",
        bluetooth::radioStateToString(radio_state),
        dev ? bluetooth_is_scanning(dev) : false,
        can_scan);
    if (can_scan && dev && !bluetooth_is_scanning(dev)) {
        bluetooth_scan_start(dev);
    }
}

void BtManage::onHide(AppContext& app) {
    lock();
    if (btDevice) {
        bluetooth_remove_event_callback(btDevice, onKernelBtEvent);
        btDevice = nullptr;
    }
    isViewEnabled = false;
    unlock();
}

extern const AppManifest manifest = {
    .appId = "BtManage",
    .appName = "Bluetooth",
    .appIcon = LVGL_ICON_SHARED_BLUETOOTH,
    .appCategory = Category::Settings,
    .createApp = create<BtManage>
};

LaunchId start() {
    return app::start(manifest.appId);
}

} // namespace tt::app::btmanage
