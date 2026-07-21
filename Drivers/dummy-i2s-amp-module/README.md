# Dummy I2S class-D amplifiers (MAX98357A, NS4168)

A driver covering any I2S class-D speaker amplifier with no I2C/register
interface -- just an I2S data line plus an optional GPIO enable (SD) pin --
wired as an output-only `AUDIO_CODEC_TYPE` device. Has no I2C parent; the device
sits standalone in the devicetree and only references the I2S controller by
name. Covers the Maxim MAX98357A and NSIway NS4168.

Since these amps have no hardware volume/mute registers, volume and mute are
applied in software on the PCM stream via `esp_codec_dev`'s software volume
handler.

See https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf
And https://datasheet.lcsc.com/lcsc/2110191830_NSIWAY-NS4168_C965904.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
