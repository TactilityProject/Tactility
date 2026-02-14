#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Affects LVGL widget style */
typedef enum {
    /** Ideal for very small non-touch screen devices (e.g. Waveshare S3 LCD 1.3") */
    UiDensityCompact,
    /** Nothing was changed in the LVGL UI/UX */
    UiDensityDefault
} UiDensity;

/** @deprecated use UiDensity */
typedef enum {
    UiScaleSmallest, // UiDensityCompact
    UiScaleDefault // UiDensityDefault
} UiScale;

/**
 * @deprecated Use UiDensity
 * @return the UI scaling setting for this device.
 */
UiScale tt_hal_configuration_get_ui_scale();

/** @return the UI scaling setting for this device. */
UiDensity tt_hal_configuration_get_ui_density();

#ifdef __cplusplus
}
#endif
