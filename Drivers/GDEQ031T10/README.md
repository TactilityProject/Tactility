# GDEQ031T10

Display driver for the GoodDisplay GDEQ031T10, a 3.1" 320x240 SPI e-paper panel
(UC8253-family controller) used by the LilyGO T-Deck Pro/Max.

The command sequence (panel setting, power on/off, fast/partial LUT timings, deep
sleep) is ported from the vendor reference driver in
[Xinyuan-LilyGO/T-Deck-MAX](https://github.com/Xinyuan-LilyGO/T-Deck-MAX)
(`examples/Elink_paper/GDEQ031T10_Arduino/Display_EPD_W21.cpp`).

Unlike `EPDiyDisplay` (which targets parallel-bus "EPD47"-style panels via the
`epdiy` library), this panel uses a simple 4-wire SPI interface (CS/DC/RST/BUSY +
SCK/MOSI), so it's driven directly with ESP-IDF's `spi_master` and `gpio` APIs
rather than `epdiy` or `esp_lcd_panel`.

Each LVGL flush triggers a full panel refresh: the controller is re-initialized,
the previous ("old") and new framebuffers are written, a refresh is triggered, and
the panel is powered off again, matching the vendor driver's per-refresh power
cycle. Four refresh modes are available, trading speed for quality:

- `Full` (~3s) - best quality, used by default
- `Fast` (~1.0s)
- `Slow` (~1.5s)
- `Partial` (~0.5s) - most ghosting

`requestFullRefresh()` forces the next flush to use `Full` mode, useful for
clearing up ghosting accumulated from a run of fast/partial refreshes.
