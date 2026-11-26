# SX126x driver module

Kernel driver for the Semtech SX1262 sub-GHz LoRa and (G)FSK transceiver, built on
[RadioLib](https://github.com/jgromes/RadioLib). It registers a `radio` device
(see `<tactility/drivers/radio.h>`) that supports LoRa and FSK for TX/RX and LR-FHSS for TX.

The device is a child of an SPI controller. The chip-select line comes from the parent's
`cs-gpios` entry matching the node's unit address; the `reg` property must match that
unit address (checked at device start). The reset, busy and DIO1 lines must be
SoC GPIOs (the RadioLib HAL drives them directly). The optional enable line can live on any
GPIO controller, including IO expanders. Antenna selection is provided by an antenna-switch
device referenced from the radio node.

Example (LilyGO T-Deck Max):

```dts
rf_switch: rf-switch {
    compatible = "tactility,gpio-antenna-switch";
    gpios = <&xl9555 4 GPIO_FLAG_NONE>;
    default-antenna = <0>; // External is low, internal is high.
};

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
        antenna-switch = <&rf_switch>;
        tcxo-millivolts = <2400>;
        dio2-as-rf-switch;
    };
};
```

Notes:

- Existing device trees can keep using `pin-antenna-select`; it retains its start/stop behavior.
  New designs should use `antenna-switch` for runtime antenna selection.
- The antenna-switch device selects its default antenna when it starts. Use
  `radio_set_antenna()` to change it while the radio is off, and `radio_get_antenna()` to read
  the selected index. Starting the radio device powers the module and runs a GPIO-only
  probe (reset pulse, then wait for the chip to drive BUSY low), so an absent or
  unpowered module fails at device start. The modem itself is initialized when the
  radio is enabled via `radio_set_enabled()`, which requires a modulation to be set first.
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
  `RADIO_PARAMETER_CURRENT_LIMIT` (up to 140 mA) to reach full output power.
- Listen-before-talk (`RADIO_PARAMETER_LBT`): before each transmission, LoRa uses channel activity detection and
  FSK compares the RSSI to `RADIO_PARAMETER_LBT_RSSI_THRESHOLD`. A busy channel backs off 20-200 ms, up to 5 times,
  before the transmission reports `RADIO_TRANSMISSION_STATE_CHANNEL_BUSY`.
- `radio_get_rssi()` and `radio_is_receiving()` are served by the radio thread without leaving RX. The RSSI is
  sampled during duty-cycled RX, so it's an approximation of the noise floor.
- Hotplug: a node with status "okay" is only started when the chip is found. Before the device starts, the probe
  powers and resets the module and reads the version string register (`0x0320`). While the radio is on, the radio
  thread reads that register. While the radio is off, a started device counts as present. The probe skips the bus
  when another started device on the same SPI controller uses the same chip-select line.
- `pin-rx-enable` is for modules whose RF switch has a separate RX enable line next to DIO2 (e.g. Seeed Wio-SX1262).
  RadioLib drives it high while receiving and low otherwise.
- Radio activity is logged at INFO level (state changes, modem config, TX/RX events);
  per-call detail sits at DEBUG, which is compiled out unless the sdkconfig log level
  is raised.

## Roadmap / not yet implemented

- FSK addressed transmission is not exposed through the radio API yet.
- Antenna-presence detection. The SX1262 has no hardware antenna-detect pin (unlike, e.g.,
  the u-blox SARA modules' dedicated ANT_DET ADC circuit), and the T-Deck Max antenna
  connector is not hot-swap-rated. The intended approach is a software heuristic on the
  radio thread — watching the RX noise floor / RSSI for the signature of a
  disconnected/open PA load and disabling TX at the driver layer when detected — so the
  protection lives below the app/OS. This is unproven and deliberately not built yet;
  feedback on the method is welcome before implementing it.
