#pragma once

struct Device;

enum class Tab5Variant {
    Unknown,
    V1,  // Older variant: ILI9881C display + GT911 touch (see devices_v1.cpp)
    V2,  // Newer variant (default): ST7123 display + in-cell touch (see devices_v2.cpp)
};

// Populated once the device_listener callback in display_detect.cpp has detected which
// panel/touch variant is present (gated on i2c0 + io_expander0 both reaching
// DEVICE_EVENT_STARTED). Safe to call from Configuration.cpp's createDevices()/createTouch():
// kernel_init() (which runs the listener to completion) always finishes before hal::init()
// invokes createDevices().
[[nodiscard]] Tab5Variant tab5_get_variant();

// Registers/unregisters the device_listener (display_detect.cpp) that detects the display/touch
// variant, keyboard and power control devices and dynamically constructs them. Called from
// module.cpp's start()/stop().
void tab5_detect_start();
void tab5_detect_stop();

// --- detect.cpp internals, exposed only for display_detect.cpp's on_display_detect_event() ---

// Records the variant found by probe_variant() below - the only writer of the state
// getDetectedTab5Variant() reads.
void tab5_set_variant(Tab5Variant variant);

// Pulses only the LCD + touch reset bits on io_expander0 (see the pinout table in
// Configuration.cpp). The other io_expander0 pins (speaker enable, RF switch, 5V bus, camera
// reset) are unrelated to display bring-up and stay owned by Configuration.cpp's initExpander0().
bool pulse_display_reset_pins(Device* io_expander0);

// Probes i2c0 for a known touch controller address to determine which display/touch variant is
// present. Must be called after pulse_display_reset_pins().
Tab5Variant tab5_probe_variant(Device* i2c0);
