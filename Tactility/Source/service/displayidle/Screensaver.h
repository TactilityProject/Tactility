#pragma once

#include <lvgl.h>

namespace tt::service::displayidle {

/**
 * Base class for screensaver implementations.
 * Screensavers are responsible for creating their visual elements
 * on the provided overlay and updating them each tick.
 */
class Screensaver {
public:
    Screensaver() = default;
    virtual ~Screensaver() = default;
    Screensaver(const Screensaver&) = delete;
    Screensaver& operator=(const Screensaver&) = delete;
    Screensaver(Screensaver&&) = delete;
    Screensaver& operator=(Screensaver&&) = delete;

    /**
     * Start the screensaver, creating visual elements on the overlay.
     * @param overlay The parent LVGL object to create elements on
     * @param screenW Screen width in pixels
     * @param screenH Screen height in pixels
     */
    virtual void start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) = 0;

    /**
     * Stop the screensaver, cleaning up any internal state.
     * Note: LVGL objects are deleted by the parent overlay, but any
     * internal references should be cleared here.
     */
    virtual void stop() = 0;

    /**
     * Update the screensaver animation for one frame.
     * Called periodically while the screensaver is active.
     * @param screenW Screen width in pixels
     * @param screenH Screen height in pixels
     */
    virtual void update(lv_coord_t screenW, lv_coord_t screenH) = 0;
};

} // namespace tt::service::displayidle
