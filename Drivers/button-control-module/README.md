# Button Control

Kernel driver for navigating apps with 1 or 2 GPIO buttons. Kernel-driver counterpart to
`Drivers/ButtonControl`, exposing a `KEYBOARD_TYPE` device (`tactility/drivers/keyboard.h`)
instead of an LVGL encoder indev.

Devicetree binding: `tactility,button-control` (see `bindings/tactility,button-control.yaml`).

## Modes

- **One-button** (`pin-secondary` absent): short press = `LV_KEY_NEXT`, long press = `LV_KEY_ENTER`.
- **Two-button** (`pin-secondary` present): primary short = `LV_KEY_ENTER`, primary long =
  `LV_KEY_ESC`, secondary short = `LV_KEY_NEXT`, secondary long = `LV_KEY_PREV`.

Short vs. long press is only decided when the button is released, by comparing the hold
duration against `long-press-ms` - not at the moment it's pressed.

## Example

```dts
buttons {
    compatible = "tactility,button-control";
    pin-primary = <&gpio0 37 GPIO_FLAG_NONE>;
    pin-secondary = <&gpio0 39 GPIO_FLAG_NONE>;
};
```

## License

[Apache License Version 2.0](LICENSE-Apache-2.0.md)