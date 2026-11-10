# CST9220 capacitive touch

Tactility touch driver for the Hynitron CST9220, built on ESP-IDF's
`esp_lcd_touch_cst9220` component. The CST9217 used on the ESP-Mosaico is
register-compatible and uses the same driver.

The I2C bus is the device's parent (per `i2c-device.yaml`). The controller sits
at the fixed 7-bit address `0x5A`; the binding's `reg` property is passed
through to the panel IO.

## Devicetree example

```dts
i2c0 {
    compatible = "espressif,esp32-i2c-master";
    port = <I2C_NUM_0>;
    clock-frequency = <400000>;
    pin-sda = <&gpio0 56 GPIO_FLAG_PULL_UP>;
    pin-scl = <&gpio0 3 GPIO_FLAG_PULL_UP>;

    touch0 {
        compatible = "hynitron,cst9220";
        reg = <0x5A>;
        x-max = <479>;
        y-max = <479>;
        pin-interrupt = <&gpio0 6 GPIO_FLAG_NONE>;
    };
};
```

`pin-reset` defaults to `GPIO_PIN_SPEC_NONE` because the Mosaico does not wire
the controller's reset line.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
