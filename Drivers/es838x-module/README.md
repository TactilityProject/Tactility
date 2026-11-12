# ES838x audio codecs

Drivers for the `ES8388` and `ES8389` audio codecs by Everest Semiconductor, each wired as a dual
in/out `AUDIO_CODEC_TYPE` device (speaker output + line/mic input). The I2C bus is
the device's parent; the I2S controller carrying audio data is referenced via a devicetree phandle.

Wraps Espressif's `esp_codec_dev` ES8388 and ES8389 implementations. The ES8389 additionally
supports an optional speaker amplifier enable pin.

See https://www.everest-semi.com/pdf/ES8388%20DS.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
