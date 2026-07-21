# AW88298 speaker amplifier

A driver for the `AW88298` smart speaker power amplifier by Awinic, wired as an
output-only `AUDIO_CODEC_TYPE` device. The I2C bus is the device's parent; the I2S
controller carrying audio data is referenced via a devicetree phandle.

Wraps Espressif's `esp_codec_dev` AW88298 implementation. If a hardware reset
(`pin-reset`) is wired, this driver pulses it itself before opening the codec,
rather than handing it to `esp_codec_dev`'s own reset handling -- the vendor
driver treats a reset pin index of `0` as "no pin wired" and silently skips the
reset, which doesn't match the `audio_codec_adapter_new_gpio()` convention used
elsewhere in this codebase (a single-pin array is always referenced at index 0).

See https://www.awinic.com/en/productDetail/AW88298

License: [Apache v2.0](LICENSE-Apache-2.0.md)
