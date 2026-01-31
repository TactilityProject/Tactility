#include "LvglTask.h"
#include "hal/SdlDisplay.h"
#include "hal/SdlKeyboard.h"
#include "hal/SimulatorPower.h"
#include "hal/SimulatorSdCard.h"

#include <src/lv_init.h> // LVGL
#include <Tactility/hal/Configuration.h>

#define TAG "hardware"

using namespace tt::hal;

static bool initBoot() {
    lv_init();
    lvgl_task_start();
    return true;
}

static void deinitPower() {
    lvgl_task_interrupt();
    while (lvgl_task_is_running()) {
        tt::kernel::delayMillis(10);
    }

#if LV_ENABLE_GC || !LV_MEM_CUSTOM
    lv_deinit();
#endif
}

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        std::make_shared<SdlDisplay>(),
        std::make_shared<SdlKeyboard>(),
        std::make_shared<SimulatorPower>(),
        std::make_shared<SimulatorSdCard>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices,
    .uart = {
        uart::Configuration {
            .name = "/dev/ttyUSB0",
            .baudRate = 115200
        },
        uart::Configuration {
            .name = "/dev/ttyACM0",
            .baudRate = 115200
        }
    }
};
