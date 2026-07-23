# GDEQ031T10 Display Driver

A kernel driver for the GoodDisplay `GDEQ031T10`, a 3.1" 240x320 SPI e-paper panel
(UC8253-family controller) used by the LilyGO T-Deck Pro/Max.

The command sequence (panel setting, power on/off, fast/partial LUT timings, deep
sleep) is ported from the vendor reference driver in
[Xinyuan-LilyGO/T-Deck-MAX](https://github.com/Xinyuan-LilyGO/T-Deck-MAX)
(`examples/Elink_paper/GDEQ031T10_Arduino/Display_EPD_W21.cpp`). 

License: [Apache v2.0](LICENSE-Apache-2.0.md)
