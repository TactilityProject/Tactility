# SPM1423 PDM microphone

A driver for the `SPM1423` PDM MEMS microphone (and compatible PDM mics), wired
as an input-only `AUDIO_CODEC_TYPE` device. Has no I2C/register interface -- just
a PDM data line -- so there is no I2C parent; the device sits standalone in the
devicetree and only references the I2S controller by name. PDM RX is hardware-
restricted to I2S controller 0.

See https://www.tdk-electronics.tdk.com/download/531768/6da4de5620b3a2b7d4a0e0e26e5c0e0a/spm1423hm4h-b-datasheet.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
