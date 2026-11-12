# lilygo-t5s3-epd-module

Kernel drivers for the 4.7" 960x540 `ED047TC1` e-paper panel of the LILYGO T5 4.7 Inch E-Paper S3 (V2.3/V2.4).

The drivers only work on this board. The pins of the panel, the 74HCT4094 shift register that carries the panel supplies and the sequence of the panel are fixed in the code, so the devicetree node only has the pixel clock as property.

There are two drivers. A devicetree uses one of them, the device `lilygo-t5-epd47-s3` uses the grayscale one and has the black and white one commented out in its devicetree:

- `lilygo,t5s3-display` (`t5s3_display.cpp`): 16 shades from black to white, dithered between neighbouring shades. Each pixel whose target shade changes gets a dose of frames, in the manner of [InkBoy](https://github.com/kuslota/InkBoy): black is driven dark for 18 frames, white and the grays are driven light for 15 frames, which erases what the pixel showed before, and the grays then rest for 0 to 3 frames and are driven dark for 1 to 8 frames. Each frame of rest makes a gray a little darker, which gives 14 grays close to each other, so the dither is hardly visible. Every gray is reached the same way, so moving content leaves almost no trails. Frames of 40 ms cover the whole screen, so all changes show at once: black and white within a few frames, a gray after about 0.9 s. `refresh()` flashes the panel and draws the content again, and the driver does that by itself when the content changed without touches for 10 minutes. A frame that stalls, for example while the flash is written, makes the grays it interrupted start again. `draw_bitmap()` does not wait for the panel and pixels that do not change are never driven. It needs the pixel clock of 20 MHz of its binding.
- `lilygo,t5s3-display-bw` (`t5s3_display_bw.cpp`): black and white with a pixel state machine, like the M5Stack PaperS3 driver. Gray levels are dithered. It is the fastest.

Shared and internal files:

- `t5s3_epd_board.cpp`: the signalling of the board. It controls the shift register, the CKV gate clock (RMT peripheral) and the row transfer (LCD peripheral in i80 mode), and sends one frame at a time.

A pixel keeps its drive until its row is addressed again, so frames are padded to a fixed period. The doses, the frame counts and the shade brightness in the code were measured with a camera on the panel.

## License

The board signalling is derived from [epdiy](https://github.com/vroland/epdiy) and is licensed under the
[LGPL v3.0 or later](LICENSE-LGPL-3.0.md) (the LGPL supplements the [GPL v3.0](../../Documentation/LICENSE-GPL-3.0.md)):

- `source/t5s3_epd_board.cpp`
- `source/t5s3_epd_board.h`

These files follow the sequences and timings of epdiy. Each of them has an `SPDX-License-Identifier: LGPL-3.0-or-later` header.

The rest of the module (`source/t5s3_display.cpp`, `source/t5s3_display_bw.cpp`, `source/module.cpp`, the bindings and the headers in `include/`) is licensed under [Apache License v2.0](LICENSE-Apache-2.0.md). The pixel state machine in `t5s3_display_bw.cpp` follows the M5Stack PaperS3 display driver of Tactility. The dose method of `t5s3_display.cpp` follows the description of InkBoy (MIT), no code of InkBoy is used.

A project that contains this module has to follow the LGPL for the files above.
