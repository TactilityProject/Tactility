# ESP32 audio codec

Provides `audio_codec_adapters.h`: glue that lets a codec driver build an `esp_codec_dev` control/data/GPIO
interface (`audio_codec_ctrl_if_t` / `audio_codec_data_if_t` / `audio_codec_gpio_if_t`) directly from Tactility's
own I2C controller, I2S controller, and GPIO pin devices, so a codec driver only ever needs
`device_get_parent()`/`device_find_by_name()` plus a handful of devicetree properties -- it never touches the platform
I2C/I2S/GPIO APIs directly.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
