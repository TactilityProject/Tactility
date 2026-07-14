# ES8311 audio codec

A driver for the `ES8311` audio codec by Everest Semiconductor, wired as a dual
in/out `AUDIO_CODEC_TYPE` device (speaker output + microphone input on the same
chip). The I2C bus is the device's parent; the I2S controller carrying audio data
is referenced via a devicetree phandle.

Wraps Espressif's `esp_codec_dev` ES8311 implementation.

See https://www.everest-semi.com/pdf/ES8311%20PB.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
