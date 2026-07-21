# ES7210 microphone ADC

A driver for the `ES7210` 4-channel microphone ADC by Everest Semiconductor,
wired as an input-only `AUDIO_CODEC_TYPE` device. The I2C bus is the device's
parent; the I2S controller carrying the (4-slot TDM) audio data is referenced by
name.

Always configures the hardware for the full 4-slot TDM frame regardless of how
many mics are actually wired (`mic-mask`), then demuxes down to just the active
mic slots in software -- `es7210_set_fs()` silently halves the configured bit
depth when asked for 2 or fewer TDM channels, which corrupts audio if the mic
count is passed straight through.

Wraps Espressif's `esp_codec_dev` ES7210 implementation.

See https://www.everest-semi.com/pdf/ES7210%20PB.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
