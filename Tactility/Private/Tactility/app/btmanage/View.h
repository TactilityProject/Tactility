#pragma once

#include "./Bindings.h"
#include "./State.h"

#include <cstdint>
#include <lvgl.h>

namespace tt::app::btmanage {

class View final {

    Bindings* bindings;
    State* state;
    // Passed through to onBtToggled/onScanToggled/onPairPeer via lv_obj user_data - see
    // Bindings.h. Set in init(), before any callback can fire.
    void* context = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* enable_switch = nullptr;
    lv_obj_t* enable_on_boot_switch = nullptr;
    lv_obj_t* scanning_spinner = nullptr;
    lv_obj_t* peers_list = nullptr;

    void updateBtToggle();
    void updateEnableOnBootToggle();
    void updateScanning();
    void updatePeerList();

    void createPeerListItem(const bluetooth::PeerRecord& record, bool isPaired, size_t index);

    static void onConnect(lv_event_t* event);

public:

    View(Bindings* bindings, State* state) : bindings(bindings), state(state) {}

    void init(void* context, lv_obj_t* parent);
    void update();
};

} // namespace tt::app::btmanage
