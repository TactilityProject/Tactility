# Audio stream module

Defines `AUDIO_STREAM_TYPE` and `AudioStreamApi`: the high-level, full-duplex
audio API that apps (including ELF side-loaded apps) and `AudioService` should
use instead of talking to an `AUDIO_CODEC_TYPE` device directly. Binds to one
input-capable and/or one output-capable codec device (found automatically at
start; see `Drivers/audio-codec-module`) and adds on top of it:

- Resampling, so the same app code works at any requested sample rate
  regardless of what rate the bound codec natively runs at.
- Shared volume/mute/enable state per direction, with a change callback
  (`AudioStreamChangeCallback`) that `AudioService` subscribes to.
- A single always-present device (`audio-stream0`), constructed unconditionally
  at module-start time before any devicetree codec exists, and bound lazily on
  first use -- so boards with no audio hardware at all still get a harmless,
  inert device rather than nothing.

`open_input`/`open_output` return a stream handle; `read`/`write` on that
handle are blocking and must be called from the caller's own task, never from
the main/LVGL thread.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
