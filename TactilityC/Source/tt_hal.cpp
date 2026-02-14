#include "tt_hal.h"

#include <Tactility/Tactility.h>
#include <Tactility/hal/Configuration.h>

extern "C" {

UiDensity tt_hal_configuration_get_ui_density() {
    auto density = tt::hal::getConfiguration()->uiDensity;
    return static_cast<UiDensity>(density);
}

UiScale tt_hal_configuration_get_ui_scale() {
    auto density = tt::hal::getConfiguration()->uiDensity;
    return static_cast<UiScale>(density);
}

}
