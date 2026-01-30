#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tlora_pager_driver;

static error_t start() {
    /* We crash when destruct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct(&tlora_pager_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_destruct(&tlora_pager_driver) == ERROR_NONE);
    return ERROR_NONE;
}

/** @warn The variable name must be exactly "device_module" */
struct Module device_module = {
    .name = "LilyGO T-Lora Pager",
    .start = start,
    .stop = stop
};

}
