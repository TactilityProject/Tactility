#pragma once

/**
 * Creates required aliases for the devicetree generation.
 * @param compatible_name the "compatible" value for the related driver
 * @param config_type the internal configuration type for a device
 */
#define DEFINE_DEVICETREE(compatible_name, config_type) typedef config_type compatible_name##_config_dt;
