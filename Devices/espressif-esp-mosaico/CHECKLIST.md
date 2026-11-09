# ESP-Mosaico V1.2 bring-up checklist

Checklist of the **internal** hardware the official ESP-Mosaico BSP covers, with Tactility's
current state. The V1.2 OV3640 and V1.4 SC101IOT camera modules and other external modules are
out of scope for now.

Legend: ✅ working · 🟡 present but untested/incomplete · ❌ missing

Pin assignments below are from the official V1.2 user guide (`ESP-Mosaico V1.2 → MCU and Pin
Assignment`), not from guesswork. V1.0 differs: LCD_RST/LCD_SCL are swapped (42/44) and the
internal I2C is on GPIO0/1.

## Display and input

| Subsystem | Hardware | Pins | Tactility | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| Display | CO5300 480x480 QSPI AMOLED | CS 50, SCL 42, D0-D3 36/51/35/9, RST 44, TE 43 | `Drivers/co5300-module` | ✅ | 80 MHz QSPI, TE-synced flushes, full-frame render mode in PSRAM. Tear-free in practice. |
| Display shape | Rounded corners, ~6 mm | — | DTS `shape`/`corner-radius` | ✅ | 74 px (6 mm at ~314 dpi) drives the LVGL insets. |
| Touch | CST9220-series (CST9217-compatible) | I2C0 0x5A, INT 6 | `Drivers/cst9220-module` | ✅ | INT pulses; reads are unconditional. Idle `0xFF` reports are silenced at log level. |
| Backlight | Panel command 0x51 (no GPIO) | — | CO5300 backlight child | ✅ | Settings/DISPLAY service controls it. |
| AI button | Momentary, active-low | 7 | — | — | No software action. GPIO7 is only configured as the deep-sleep wake source (see Deep sleep row); `button-control` was dropped from the DTS. |
| POWER button | SAM8108 hardware power control | — | — | ✅ | Hardware, no software needed for on/off. |
| BOOT button | Strapping pin | 61 | — | ✅ | ROM download mode. |
| Vibration motor | ERM via low-side MOSFET, active-high | 8 | `Drivers/pwm-vibration-module` + Vibration settings app | ✅ | PWM strength + timed pulses; enable/strength persisted and gated in Settings → Vibration (Test button). A short boot pulse uses the configured strength and is skipped when vibration is disabled (`CONFIG_TT_VIBRATION_ON_BOOT`). |

## Audio

| Subsystem | Hardware | Pins | Tactility | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| Codec | ES8311 | I2C0 0x19 | `Drivers/es8311-module` | ✅ | Speaker path verified with a 1 kHz test tone (temporary boot check, since removed). |
| Speaker amp | NS4150B, enable | 45 | ES8311 `pin-pa` | ✅ | Driven by the codec driver. |
| I2S | BCK 37, WS 49, DOUT 40, DIN 52, MCLK 54 | — | `espressif,esp32-i2s` | ✅ | Configured in DTS. |
| Mic | LMA2718B331 via ES8311 ADC | — | ES8311 input path | ✅ | Verified with the AudioRecorder app: 23.7 s of 16 kHz mono 16-bit WAV recorded to `/sdcard/tactility/user/app/tactility.audiorecorder/` and played back through the speaker. |
| Audio service/settings | — | — | `Drivers/audio-stream-module` | ✅ | Audio settings app present; output enable/volume/mute work. Input mute is applied in the audio-stream layer because the ES8311 has no ADC mute. |

## Sensors and power

| Subsystem | Hardware | Pins | Tactility | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| IMU | BMI270 | I2C0 0x69 | `Drivers/bmi270-module` | ✅ | Confirmed with auto-rotate. |
| Magnetometer #2 | BMM150 | I2C0 0x11 | `Drivers/bmm150-module` | ✅ | Verified with a magnet (values respond; strong magnets saturate into overflow). Mount matrix still identity. |
| Magnetometer #3 | BMM150 | I2C0 0x12 | `Drivers/bmm150-module` | ✅ | Same as above. |
| Fuel gauge | BQ27220 | I2C0 0x55 | `Drivers/bq27220-module` | ✅ | Voltage/SoC reported. Accuracy on the 65 mAh cell unverified. |
| Charger | TP4057 | — | — | ✅ | Hardware only; charge LED is hardware-driven. |
| VCC_3V3 rail | Load switch enable, active-low | 60 | `gpio-hog` in DTS | ✅ | Display and external rails work. |
| Deep sleep | AI button wake (GPIO7, active-low + internal pull-up) | 7 | `espressif,esp32-sleep`, Power app | ✅ | Panel is blanked on the DEEP_SLEEP_PREPARE event; wake is a full boot, which is inherent to deep sleep. |
| POWER_SWITCH | Open-drain shutdown request, active-low | 57 | `Drivers/gpio-poweroff-module` | ✅ | High-impedance while idle; the driver asserts the pin's logical active level and the DTS `GPIO_FLAG_ACTIVE_LOW` flag makes that a physical low. Power app → "Power off" cuts the rails; the RTC crash record is discarded on the next boot because the RTC domain loses power. |
| USB-C CC | HUSB320 | I2C (not wired) | — | — | Not needed for current functionality. |

## Storage and connectivity

| Subsystem | Hardware | Pins | Tactility | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| NOR flash | GD25Q128E 16 MB | SPI0/1 | ESP-IDF | ✅ | 16 MB, DIO 80 MHz. Holds the bootloader, app and NVS. Its 9.8 MB `data` partition is unused because user data lives on the NAND (`storage.userDataLocation=SD` uses the no-data partition table). |
| NAND flash | GD5F1GM7 1 Gbit (128 MB) | CLK 20, D 21, Q 22, CS 23, HOLD 24, WP 25 | `Drivers/spi-nand-module` (`espressif,spi-nand`) | ✅ | FAT + Dhara FTL, mounted at `/sdcard` as Tactility's user-data volume (`storage.userDataLocation=SD`). 93.2 MiB usable (Dhara journal + `gc-factor=4` GC reserve; see README). First boot formats (~22 s); later boots mount in under 50 ms. NOR `data` partition is unused in this mode. |
| Wi-Fi | ESP32-S31 2.4 GHz Wi-Fi 6 | — | platform-esp32 | ✅ | Enabled via `hardware.target`/BT flags. |
| Bluetooth | BT 5.4 LE + BR/EDR | — | platform-esp32 | ✅ | NimBLE enabled; radio off/on from Settings cycles cleanly. S31 note: `esp_bt_controller_deinit()` asserts in `btdm_osal_elem_mempool_deinit()` (IDF 6.1 preview), so the controller stays initialized and is only disabled between sessions. |
| 802.15.4 | Thread/Zigbee | — | — | 🟡 | Silicon supports it; no Tactility radio service enabled. |
| USB device | USB-OTG HS, Type-C | — | `usbdevice0` + TinyUSB | ✅ | MSC on demand + boot-time CDC console (`console-at-boot`). USB drive mode exposes the NAND FAT volume via esp_tinyusb's block-device backend (host sees 2048-byte logical blocks); the local `/sdcard` mount is suspended while the host owns it, and writes are flushed to Dhara's journal on a 500 ms debounce and again when leaving drive mode. Copy and delete both persist. |
| USB host | Same OTG controller/PHY | — | `usbhost0` (not in DTS) | ❌ | S31 supports host mode, but the Mosaico has one port/PHY shared with device mode; needs OTG role handling and VBUS/ID verification. |

## Developer workflow

| Item | Status | Notes |
| --- | --- | --- |
| Serial console | ✅ | CDC-ACM console over the Type-C port ("Tactility Device"). Windows enumerates it as a COM port. |
| Flashing | ✅ | Enter download mode first: with the device off, hold BOOT (GPIO61) while powering it on, then release. BOOT does not reset a running board. `TT:BOOTDL` on the running console is the alternative. Then `idf.py flash`. The device sdkconfig selects `--before=no-reset` because the USB-OTG ROM port has no DTR/RTS reset lines. The esptool stub works on S31, so a full image writes in about 30 s (`CONFIG_ESPTOOLPY_NO_STUB=n`). For logs, run `idf.py -p COMx monitor`. The firmware mirrors logs regardless of DTR/RTS, and output starts once the app console enumerates, so bootloader and early boot logs need a UART console. |
| Auto-download over CDC | ✅ | The console RX handler sets `LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT` (S31's equivalent of `RTC_CNTL_FORCE_DOWNLOAD_BOOT`) and resets. The ROM then enumerates its USB-OTG download interface on the same Type-C port. |
| External app/SDK support for S31 | ✅ | SDK platform `platform-esp32s31` (generic device `Devices/generic-esp32s31` + CI matrix entry + `sdkconfig.app.esp32s31`). `tt app bir --architecture esp32s31` builds, installs over the dev server, and runs (HelloWorld, Snake, AudioRecorder, Doom verified). PPA and `esp_cache_msync` are exported to apps on S31 as on P4, which the Doom port needs. |

## Known good state

- Builds against ESP-IDF 6.1 (`C:\esp\v6.1\esp-idf`) with `idf.py build`.
- App image ~4.9 MB, 19% free in the 6 MB app partition.
- Boots to the Tactility launcher, touch works, display is tear-free at 80 MHz with TE.
- Speaker output confirmed; Audio settings app available; mic mute works (software-applied).
- Bluetooth radio toggles off and back on without crashing (30+ cycles stress-tested, including while scanning; S31 keeps the controller initialized between sessions because `esp_bt_controller_deinit()` asserts in `btdm_osal_elem_mempool_deinit()`).
- Software power-off via the Power app cuts the device rails (POWER_SWITCH/GPIO57). After a full power cycle the RTC crash record is invalid, so it is discarded silently instead of being reported as a bogus crash.
- Flashing works from boot mode: with the device off, hold BOOT while powering it on, then run `idf.py flash`. The sdkconfig uses `--before=no-reset` because the USB-OTG ROM port has no DTR/RTS reset wiring. The stub flasher is enabled, so a full image writes in about 30 s. For logs, run `idf.py -p COMx monitor`. The firmware mirrors logs regardless of DTR/RTS, and output begins when the app console enumerates, so bootloader and early boot logs need a UART console.
- The 128 MB SPI NAND is the user-data volume: FAT on Dhara FTL, mounted at `/sdcard`, visible as a 93.2 MB drive in System Info. Setup/config/app files live under `/sdcard/tactility`. The NOR `data` partition is no longer used.
- USB drive mode works for the NAND volume: Settings → USB reboots into mass storage, Windows mounts the volume, file copy and delete persist, and eject + tap returns to the OS. The volume reports healthy.
- The on-screen keyboard appears for text entry. Navigation-only keyboards (encoders, trackballs) advertise `KeyboardApi::is_text_input = false`, so they no longer make the UI think a typing keyboard is attached.
- External apps work end to end: an `esp32s31` SDK platform is built from the generic device, and apps build with `tt app bir --architecture esp32s31`, install over the dev server, and run. HelloWorld, Snake, and AudioRecorder were used to verify this.
- Doom runs on the Mosaico: the app is no longer Tab5-locked, and its rendering now derives the PPA rotation and RGB565 byte order from the display instead of hardcoding the Tab5's (0° and native order here, with the PPA output byte-swapped for the CO5300's big-endian RGB565). Colors and orientation are correct; the app scales/rotates/byte-swaps in software, so it is slower than the Tab5's hardware framebuffer path. Keyboard input still requires a USB/Bluetooth keyboard.

## Next steps (suggested order)

1. Runtime v1.0/v1.2 revision detection so one image covers both boards (only if v1.0 units exist).
2. USB host mode, if ever needed: the Type-C port shares one OTG controller/PHY with device mode, so it needs OTG role handling plus VBUS/ID verification.
