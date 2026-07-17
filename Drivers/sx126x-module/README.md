# SX126x driver module

Kernel driver for the Semtech SX1262 sub-GHz LoRa and (G)FSK transceiver, built on
[RadioLib](https://github.com/jgromes/RadioLib). It registers a `lora` device
(see `<tactility/drivers/lora.h>`) that supports LoRa and FSK for TX/RX and LR-FHSS for TX.

The device is a child of an SPI controller. The chip-select line comes from the parent's
`cs-gpios` entry matching the node's unit address; the `reg` property must match that
unit address (checked at device start). The reset, busy and DIO1 lines must be
SoC GPIOs (the RadioLib HAL drives them directly); the optional enable and antenna-select
lines can live on any GPIO controller, including IO expanders, and are driven to their
active level while the device is started.

Example (LilyGO T-Deck Max):

```dts
spi0 {
    compatible = "espressif,esp32-spi";
    cs-gpios = <&gpio0 34 GPIO_FLAG_NONE>, // 0: EPD display
               <&gpio0 48 GPIO_FLAG_NONE>, // 1: SD card
               <&gpio0 3 GPIO_FLAG_NONE>;  // 2: LoRa radio (SX1262)
    /* ... */

    radio@2 {
        compatible = "semtech,sx1262";
        reg = <2>;
        pin-reset = <&gpio0 4 GPIO_FLAG_NONE>;
        pin-dio1 = <&gpio0 5 GPIO_FLAG_NONE>;
        pin-busy = <&gpio0 6 GPIO_FLAG_NONE>;
        pin-enable = <&xl9555 1 GPIO_FLAG_NONE>;
        pin-antenna-select = <&xl9555 4 GPIO_FLAG_NONE>;
        tcxo-millivolts = <2400>;
        dio2-as-rf-switch;
    };
};
```

Notes:

- Starting the device selects the antenna path, powers the module and runs a GPIO-only
  probe (reset pulse, then wait for the chip to drive BUSY low), so an absent or
  unpowered module fails at device start. The modem itself is initialized when the
  radio is enabled via `lora_set_enabled()`, which requires a modulation to be set first.
- Bus sharing: RadioLib drives the chip-select manually across multiple transfers, so
  each exchange must be atomic on the bus. It runs inside two nested locks — the kernel
  SPI controller lock (`spi_controller_lock`, the arbiter other kernel drivers on the
  host cooperate through) as the outer lock, and ESP-IDF's per-host bus lock
  (`spi_device_acquire_bus`) as the inner one, which additionally blocks the IDF-managed
  spi_master devices (displays, SD cards) that don't take the controller lock.
- TX-done is waited for using the packet's time-on-air plus a fixed margin, so slow
  configurations (high spreading factor / narrow bandwidth) don't false-time-out.
- Over-current protection defaults to RadioLib's fail-safe 60 mA, which caps the PA
  current into a bad or disconnected antenna but also caps output below +22 dBm. A
  board-aware consumer that knows its antenna and PA can raise it via
  `LORA_PARAMETER_CURRENT_LIMIT` (up to 140 mA) to reach full output power.
- Radio activity is logged at INFO level (state changes, modem config, TX/RX events);
  per-call detail sits at DEBUG, which is compiled out unless the sdkconfig log level
  is raised.

## Roadmap / not yet implemented

- FSK addressed transmission is not exposed through the lora API yet.
- Antenna-presence detection. The SX1262 has no hardware antenna-detect pin (unlike, e.g.,
  the u-blox SARA modules' dedicated ANT_DET ADC circuit), and the T-Deck Max antenna
  connector is not hot-swap-rated. The intended approach is a software heuristic on the
  radio thread — watching the RX noise floor / RSSI for the signature of a
  disconnected/open PA load and disabling TX at the driver layer when detected — so the
  protection lives below the app/OS. This is unproven and deliberately not built yet;
  feedback on the method is welcome before implementing it.
