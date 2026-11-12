# Espressif ESP32-S31-Korvo-1

Tactility device definition for the ESP32-S31-Korvo-1 V1.1 (ESP32-S31-WROOM-3).

## Hardware

- ESP32-S31, 16 MB flash, 16 MB PSRAM
- ES8389 audio codec (I2C GPIO0/1, I2S MCLK 2, SCLK 3, LRCLK 4, DOUT 5, DIN 6), PA enable GPIO7, two NS4150B amplifiers, two analog microphones
- microSD, 4-bit SDIO: CLK 24, CMD 25, D0-D3 20-23
- WS2812 RGB LED on GPIO37
- ADC buttons PLAY/SET/VOL-/VOL+ on GPIO42
- DVP camera, 4.3" parallel RGB LCD via the ESP32-S3-LCD-EV-Board-SUB3 subboard
- USB 2.0 OTG HS, UART0 on GPIO58/59

## Status

Described in the devicetree: Wi-Fi, BLE, GPIO, I2C, I2S, SDMMC storage, ES8389 codec, USB MSC, RGB LED, ST7262 RGB LCD and GT1151 touch (through the GT911 driver).

Pins and panel timing follow the factory demo BSP. The backlight is always on (no control pin). The touch I2C address is the GT911 default and is unverified.

Missing: camera and the ADC buttons.
