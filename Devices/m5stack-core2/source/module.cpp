#include <tactility/module.h>

extern "C" {

// AXP192 rail/GPIO1 bring-up is now devicetree-configured (see m5stack,core2.dts's axp192 node
// and axp192-module's start()), replacing what used to be done by hand here via a
// DEVICE_EVENT_STARTED device_listener.
Module m5stack_core2_module = {
    .name = "m5stack-core2"
};

}
