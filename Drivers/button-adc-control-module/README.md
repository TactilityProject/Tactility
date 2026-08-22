# Button ADC Control

Kernel driver for buttons behind an ADC resistor ladder (e.g. the Xteink X4's six side
buttons on two ADC pins). Exposes a `KEYBOARD_TYPE` device (`tactility/drivers/keyboard.h`)
that translates presses into LVGL navigation keys.

Devicetree binding: `tactility,button-adc-control` (see `bindings/tactility,button-adc-control.yaml`).

Each button is defined by an ADC channel and the raw-value band it occupies; a button is
pressed while `band_low < raw <= band_high`. Each entry is
`<adc_phandle channel band_high band_low key>`; the bottom-most band uses `-2147483648`
(INT32_MIN) as its `band_low` so the ladder's near-ground rung is still inside the band.
Bands come from the values recorded on real hardware (e.g. the X4's Back 3512 /
Confirm 2694 / Left 1493 / Right ~5 and Up 2242 / Down ~5 readings), split at the
midpoints between neighbouring readings. The ADC must be configured with the same
attenuation the ranges were recorded at (X4: 11 dB, `ADC_ATTEN_DB_12` on this IDF, its
non-deprecated alias).

## Example

```dts
buttons {
    compatible = "tactility,button-adc-control";
    debounce-ms = <20>;
    buttons = <&adc0 1 3803 3103 LV_KEY_ESC>,     // Back, presses at 3512
              <&adc0 1 3103 2093 LV_KEY_ENTER>,   // Confirm, presses at 2694
              <&adc0 1 2093 749 LV_KEY_LEFT>,     // Left, presses at 1493
              <&adc0 1 749 -2147483648 LV_KEY_RIGHT>, // Right, presses at ~5
              <&adc0 2 3168 1123 LV_KEY_UP>,      // Up, presses at 2242
              <&adc0 2 1123 -2147483648 LV_KEY_DOWN>; // Down, presses at ~5
};
```

## License

[Apache License Version 2.0](LICENSE-Apache-2.0.md)
