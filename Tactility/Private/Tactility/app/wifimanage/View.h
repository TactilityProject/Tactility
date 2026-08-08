#pragma once

#include "./Bindings.h"
#include "./State.h"

#include <cstdint>
#include <lvgl.h>

namespace tt::app::wifimanage {

class View final {

    Bindings* bindings;
    State* state;
    uint32_t appInstanceId = 0;
    lv_obj_t* root = nullptr;
    lv_obj_t* enable_switch = nullptr;
    lv_obj_t* enable_on_boot_switch = nullptr;
    lv_obj_t* scanning_spinner = nullptr;
    lv_obj_t* networks_list = nullptr;
    lv_obj_t* connect_to_hidden = nullptr;

    void updateWifiToggle();
    void updateEnableOnBootToggle();
    void updateScanning();
    void updateNetworkList();
    void updateConnectToHidden();
    void createSsidListItem(const WifiApRecord& record, bool isConnecting, size_t index);

    static void showDetails(lv_event_t* event);
    static void connect(lv_event_t* event);

public:

    View(Bindings* bindings, State* state) : bindings(bindings), state(state) {}

    void init(uint32_t appInstanceId, lv_obj_t* parent);
    void update();
};


} // namespace
