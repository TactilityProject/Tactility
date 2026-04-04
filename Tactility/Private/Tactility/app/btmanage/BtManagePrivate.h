#pragma once

#include "./View.h"
#include "./State.h"

#include <Tactility/app/App.h>
#include <Tactility/Mutex.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <tactility/drivers/bluetooth.h>

namespace tt::app::btmanage {

class BtManage final : public App {

    Mutex mutex;
    Bindings bindings = { };
    State state;
    View view = View(&bindings, &state);
    bool isViewEnabled = false;
    struct Device* btDevice = nullptr;

public:

    void onBtEvent(const struct BtEvent& event);

    BtManage();

    void lock();
    void unlock();

    void onShow(AppContext& app, lv_obj_t* parent) override;
    void onHide(AppContext& app) override;

    Bindings& getBindings() { return bindings; }
    State& getState() { return state; }

    void requestViewUpdate();
};

} // namespace tt::app::btmanage
