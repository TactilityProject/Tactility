# SSD1677 E-Paper Driver

Display driver for e-paper panels of 800x480 pixels with a Solomon Systech SSD1677 controller on SPI,
such as the 3.97 inch panel of the Waveshare ESP32-S3-ePaper-3.97.

Devicetree binding: `solomon-systech,ssd1677` (see `bindings/solomon-systech,ssd1677.yaml`).

## How it draws

- The panel is driven with waveforms of the driver (register 0x32) instead of the ones in the OTP. Only pixels whose
  shade changes are driven, so updates do not flash. Black is driven dark. White and the grays are driven light at
  the same time, which erases the old content, and the grays then get a dose of dark frames for their shade.
  8 shades, dithered with a Bayer matrix. A gray that replaces black or a dark gray gets more light frames, as the
  dark shade would stay visible in it otherwise.
- The controller picks one of 4 waveforms per pixel from its bits in the two RAM planes, and waveform 0 holds. An
  update therefore runs a pass for black and the light frames and up to 2 passes for 3 grays each. Every pass ends
  with hold frames, as the voltages of the last phase stay on the panel after a pass.
- Only the rows and columns that changed are processed, and only the rows that changed are sent to the RAM.
- Frames are 5.8 ms (frame rate setting 0x88), shorter frames drive harder. Frame counts, doses and the brightness of
  the shades were measured with a camera. A change in black and white takes about 0.2 s, one with grays about 0.4 s.
- `refresh()` drives the whole panel dark and then light once and draws the content again, which removes
  ghosting. Tactility calls it after the user stopped using the device. Content that changes without input gets a
  refresh from the driver after 5 s of quiet, at most every 10 minutes. `clear()` flashes the panel to white.

The register values of the start sequence follow the example code of Waveshare
(https://github.com/waveshareteam/ESP32-S3-ePaper-3.97, MIT license). The waveform format is in the SSD1677 datasheet.

## License

[Apache License Version 2.0](LICENSE-Apache-2.0.md)
