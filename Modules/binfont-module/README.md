# binfont-module

Bitmap fonts in the [lv_font_conv](https://github.com/lvgl/lv_font_conv) binary format (`--format bin`), usable with and without LVGL.

- `<binfont/binfont.h>`: load fonts and decode glyphs
- `<binfont/generator.h>`: rasterize a TTF into the binary format on device
- `<binfont/render.h>`: draw glyphs and text into a `PixelBuffer`

For LVGL, see `lvgl_binfont_create()` in lvgl-module's `<lvgl/binfont.h>`.

## License

This module is licensed under the [Apache v2.0](LICENSE-Apache-2.0.md) license.
Uses [stb_truetype](https://github.com/nothings/stb) (public domain).
