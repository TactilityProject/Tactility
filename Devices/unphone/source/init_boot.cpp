#include "BatteryManager.h"
#include "drivers/unphone_power_switch.h"

#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_sleep.h>
#include <memory>

#include <Tactility/LogMessages.h>
#include <Tactility/Thread.h>

constexpr auto* TAG = "unPhone";

std::shared_ptr<BatteryManager> batteryManagement;
static std::unique_ptr<tt::Thread> powerThread;

enum class PowerState {
    Initial,
    On,
    Off
};

static void update_power_switch(Device* power_switch) {
    static PowerState last_state = PowerState::Initial;

    bool power_switch_on = false;
    check(unphone_power_switch_is_on(power_switch, &power_switch_on) == ERROR_NONE);

    if (!power_switch_on) {
        if (last_state != PowerState::Off) {
            last_state = PowerState::Off;
            LOG_W(TAG, "Power off");
        }

        if (!batteryManagement->isUsbPowerConnected()) { // and usb unplugged we go into shipping mode
            LOG_W(TAG, "Shipping mode until USB connects");
            batteryManagement->turnPeripheralsOff();
            batteryManagement->setShipping(true); // tell BM to stop supplying power until USB connects
        } else { // When power switch is off, but USB is plugged in, we wait (deep sleep) until USB is unplugged.
            LOG_W(TAG, "Waiting for USB disconnect to power off");
            batteryManagement->turnPeripheralsOff();
            // Deep sleep for 1 minute, then awaken to check power state again
            // GPIO trigger from power switch also awakens the device
            unphone_power_switch_enable_wake(power_switch);
            esp_sleep_enable_timer_wakeup(60000000);
            esp_deep_sleep_start();
        }
    } else {
        if (last_state != PowerState::On) {
            last_state = PowerState::On;
            LOG_I(TAG, "Power on");
        }
    }
}

static int32_t power_switch_thread_main() { // check power switch every 10th of sec
    Device* power_switch = nullptr;
    check(device_get_by_name("power_switch", &power_switch) == ERROR_NONE);

    while (true) {
        update_power_switch(power_switch);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

static void power_switch_thread_start() {
    powerThread = std::make_unique<tt::Thread>(
        "unphone_power_switch",
        4096,
        []() -> int32_t { return power_switch_thread_main(); }
    );
    powerThread->start();
}


static bool power_on() {
    batteryManagement = std::make_shared<BatteryManager>();
    if (!batteryManagement->init()) {
        LOG_E(TAG, "UnPhoneFeatures init failed");
        return false;
    }

    batteryManagement->setExpanderPower(false);

    // Turn off the device if power switch is on off state,
    // instead of waiting for the Thread to start and continue booting
    Device* power_switch = nullptr;
    check(device_get_by_name("power_switch", &power_switch) == ERROR_NONE);
    update_power_switch(power_switch);
    device_put(power_switch);

    power_switch_thread_start();

    return true;
}

bool init_boot() {
    LOG_I(TAG, LOG_MESSAGE_POWER_ON_START);

    if (!power_on()) {
        LOG_E(TAG, LOG_MESSAGE_POWER_ON_FAILED);
        return false;
    }

    return true;
}
