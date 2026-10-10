#pragma once

#include <Tactility/settings/LauncherSettings.h>

#include <lvgl.h>

#include <cstdint>
#include <functional>

namespace tt::lvgl {

using SystemBarsListenerId = uint32_t;

/**
 * Takes control of the layout of the screen's statusbar and app container, and applies the layout of the launcher settings.
 * The layout follows display rotations by itself.
 * @warning Caller must hold the LVGL lock.
 * @param[in] container the flex container that holds the statusbar and the app container
 */
void systemBarsAttach(lv_obj_t* container, lv_obj_t* statusbar, lv_obj_t* appContainer);

/**
 * Releases the objects passed to systemBarsAttach(), before they're deleted.
 * @warning Caller must hold the LVGL lock.
 */
void systemBarsDetach();

/**
 * Applies the launcher settings and the current display size again, e.g. after the settings changed.
 * Notifies the listeners when the layout changed.
 * @warning Caller must hold the LVGL lock.
 */
void systemBarsRefresh();

/**
 * @warning Caller must hold the LVGL lock.
 * @return the layout that is shown
 */
settings::launcher::SystemBarsLayout systemBarsGetLayout();

/**
 * @warning Caller must hold the LVGL lock.
 * @return the container in the side strip for an app's action buttons, or nullptr when the side layout isn't shown
 */
lv_obj_t* systemBarsGetActionContainer();

/**
 * Adds a listener that is called when the layout changed. It's called with the LVGL lock held.
 * @warning Caller must hold the LVGL lock.
 */
SystemBarsListenerId systemBarsAddListener(std::function<void()> listener);

/**
 * Removes a listener. It doesn't take any lock besides the LVGL lock, so a window's destroy_widgets callback can call it.
 * @warning Caller must hold the LVGL lock.
 */
void systemBarsRemoveListener(SystemBarsListenerId id);

}
