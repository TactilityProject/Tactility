# Audio codec module

Defines codec-agnostic `AudioCodecApi` (open/close/read/write/volume/mute/native-rate/native-channels/capabilities)
that every audio codec driver in this repo implements (ES8311, ES8388, ES7210, AW88298, the dummy I2S amps, the PDM mic).
This is intentionally low-level: no resampling or mixing, one codec device per chip. Most app code should go through
`audio_stream` (see `Drivers/audio-stream-module`) instead of talking to a codec device directly.

Also provides `audio_codec_adapters.h`: glue that lets a codec driver build an `esp_codec_dev` control/data/GPIO
interface (`audio_codec_ctrl_if_t` / `audio_codec_data_if_t` / `audio_codec_gpio_if_t`) directly from Tactility's
own I2C controller, I2S controller, and GPIO pin devices, so a codec driver only ever needs
`device_get_parent()`/`device_find_by_name()` plus a handful of devicetree properties -- it never touches the platform
I2C/I2S/GPIO APIs directly.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
