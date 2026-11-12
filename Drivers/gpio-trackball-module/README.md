# GPIO Trackball

Kernel driver for a 5-way GPIO trackball (4 direction pins + a click button), exposing a
`TRACKBALL_TYPE` device (`tactility/drivers/trackball.h`).
The device has one child device, of type `KEYBOARD_TYPE` (arrow keys and enter) or `MOUSE_TYPE`, depending on
its `TrackballSettings`. There is no child while the trackball is disabled.

Devicetree binding: `tactility,gpio-trackball` (see `bindings/tactility,gpio-trackball.yaml`).

Each direction pin is wired to a falling-edge interrupt that accumulates a signed delta;
the click pin is active-low and read on any edge. `read_delta()` drains and resets the
accumulated delta on each call.

## Example

```dts
trackball {
    compatible = "tactility,gpio-trackball";
    pin-right = <&gpio0 1 GPIO_FLAG_NONE>;
    pin-up = <&gpio0 2 GPIO_FLAG_NONE>;
    pin-left = <&gpio0 3 GPIO_FLAG_NONE>;
    pin-down = <&gpio0 4 GPIO_FLAG_NONE>;
    pin-click = <&gpio0 5 GPIO_FLAG_NONE>;
    default-mode = <TRACKBALL_MODE_MOUSE>; // optional, defaults to TRACKBALL_MODE_KEYBOARD
};
```

## License

[Apache License Version 2.0](LICENSE-Apache-2.0.md)
