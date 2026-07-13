# ES8388 audio codec

A driver for the `ES8388` audio codec by Everest Semiconductor, wired as a dual
in/out `AUDIO_CODEC_TYPE` device (speaker output + line/mic input). The I2C bus is
the device's parent; the I2S controller carrying audio data is referenced by name.

Wraps Espressif's `esp_codec_dev` ES8388 implementation.

See https://www.everest-semi.com/pdf/ES8388%20DS.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
