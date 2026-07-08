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

Each LVGL flush is staged and handed off to a dedicated refresh task (the physical
refresh takes hundreds of ms to over a second, too long to run in LVGL's flush
callback without stalling input). The task diffs the new frame against a shadow of
what the panel currently holds and, where possible, performs a windowed partial
refresh of just the changed region instead of redrawing the whole panel. A full
refresh is still done on the first draw, on an explicit `requestFullRefresh()`,
when a change covers most of the screen, or periodically to clear the ghosting
that partial updates accumulate (gated by `MAX_PARTIAL_REFRESHES`, with a
localized ghost-clear preferred over a full flash when the accumulated changes are
confined to a small region). The panel's charge pump is powered off once the
refresh task's queue goes idle, not after every single refresh. Four refresh
modes are available, trading speed for quality:

- `Full` (~3s) - best quality, used by default
- `Fast` (~1.0s)
- `Slow` (~1.5s)
- `Partial` (~0.5s) - most ghosting

`requestFullRefresh()` forces the next flush to use `Full` mode, useful for
clearing up ghosting accumulated from a run of fast/partial refreshes.
