# Rotary Button

Keyboard driver for a rotary button: a switch with up, down and press contacts on GPIO pins, such as the
one on the side of the Waveshare ESP32-S3-ePaper-3.97.

Devicetree binding: `tactility,rotary-button` (see `bindings/tactility,rotary-button.yaml`).

## Keys

- Up moves the focus to the previous widget, down to the next one.
- Press is the enter key and stays pressed while it is held, so long presses work.

The level that counts as pressed comes from the pin flags: use `GPIO_FLAG_ACTIVE_LOW` for buttons that pull
the pin low.

## Example

```dts
buttons {
    compatible = "tactility,rotary-button";
    pin-up = <&gpio0 4 GPIO_FLAG_ACTIVE_LOW>;
    pin-down = <&gpio0 6 GPIO_FLAG_ACTIVE_LOW>;
    pin-press = <&gpio0 5 GPIO_FLAG_ACTIVE_LOW>;
};
```

## License

[Apache License Version 2.0](LICENSE-Apache-2.0.md)
