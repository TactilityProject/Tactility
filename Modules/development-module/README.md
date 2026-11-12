# development-module

Development server (`development-server`): a TLS server for remote development access: a remote shell, file transfer and app management.
The C API is in `<development/server.h>`.

- `development_server_set_enabled()` starts or stops listening on TCP port 6667 and persists the choice for the next boot.
- The device has a self-signed certificate, which is generated on first use.
  `development_server_get_fingerprint()` returns its SHA-256 fingerprint, so a client can verify that it connects to the right device.
- Clients authenticate with their own self-signed certificate (mutual TLS).
  Unknown clients are accepted or rejected by the user through the `DEVELOPMENT_SERVER_PROMPT_APP_ID` app, which the firmware provides.
  The user can choose to always trust a client.
- At most 3 sessions are active at a time, and at most 1 of them is a `shell` session.

## Protocol

1. The client connects with TLS 1.2 and presents its certificate.
2. The client sends a JSON line: `{"protocol":1,"mode":"shell","client":"user@host","columns":80,"rows":24}`
   In `shell` mode, an optional `"command"` (at most 511 bytes) runs that command like `sh -c` instead of an interactive shell.
3. The server answers with a JSON line once the client is accepted: `{"status":"accepted"}`, `{"status":"rejected"}` or `{"status":"error","message":"..."}`
4. In `shell` mode, both sides then send frames: `[type u8][length u16 big-endian][payload]`, with at most 1024 payload bytes.
   - `0x01` data: terminal input from the client, terminal output from the server
   - `0x02` resize: from the client, payload is `[columns u16][rows u16]` (big-endian)
   - `0x03` exit: from the server, payload is the shell's exit code as `i32` (big-endian), after which the server closes the connection
5. In `sync` mode, the client sends requests with the same frames. Paths are absolute and UTF-8 encoded.
   - `0x10` stat `[path]`: answered with `0x11` `[type u8][size u64]`, where the type is 0 for missing, 1 for a file and 2 for a directory
   - `0x12` push `[path]`, then `0x01` data frames, then `0x14` done: answered with `0x15` ok, or `0x16` fail `[message]`.
     Parent directories are created. A failed or interrupted push removes the file.
   - `0x13` pull `[path]`: answered with `0x01` data frames and `0x14` done, or with `0x16` fail `[message]`
6. In `app` mode, the client manages apps with the same frames:
   - `0x20` install `[file name]`, then `0x01` data frames with the `.app` package, then `0x14` done: answered with `0x15` ok, or `0x16` fail `[message]`
   - `0x21` uninstall `[package id]`: answered with `0x15` ok, also when the package isn't installed, or `0x16` fail `[message]`
   - `0x22` run `[app id]`: starts the app, or restarts it when it's in the foreground. Answered with `0x15` ok, or `0x16` fail `[message]`

7. In `remote` mode, the device streams its display until the client disconnects, and the client can send input. Only one remote session runs at a time.
   The device needs external RAM, because it keeps the whole display image in memory. Without it, or without a display, the device answers `0x16` fail `[message]` after accepting.
   The display is observed through `display_set_hook()`, so everything that draws through the kernel display API is streamed, also while LVGL is stopped. Content that was drawn before the session started only appears after the next draw.
   Frames from the device can have up to 16384 bytes of payload. Frames from the client stay within 1024 bytes. All numbers are big-endian.
   - `0x30` info and `0x32` state (device to client, 11 bytes): `[width u16][height u16][color format u8][swap xy u8][mirror x u8][mirror y u8][invert u8][on u8][rotation u8]`.
     The device sends the info first and the state whenever one of these changes. The size is the size of the image that draw calls address, so it follows swap xy.
     The orientation byte describes the UI, not the pixels: bits 0 and 1 are the rotation of the UI in quarter turns clockwise, and bit 2 is set when the display driver gets pixels that were rotated in software.
     The client rotates the pixels clockwise by the rotation when bit 2 is set to show the UI upright, and the size stays that of the pixels that were drawn.
     The touch device has the native orientation of the display, and the UI maps its positions by rotating them clockwise by the rotation, so the client converts touch positions back.
     The color format is `DisplayColorFormat`: 1 BGR565, 2 BGR565 swapped, 3 RGB565, 4 RGB565 swapped, 5 RGB888, 6 grayscale. A monochrome display is sent as grayscale (0 black, 255 white), because the device unpacks its 1 bit pixels.
   - `0x31` rectangle (device to client): `[x u16][y u16][width u16][height u16][flags u8][pixels]`. Bit 0 of the flags means that the pixels are a zlib stream.
     The pixels are rows of the rectangle without padding in the color format of the display. A device without the compressor sends them raw.
   - `0x40` enable input (client to device): `[mask u8]` with bit 0 keyboard and bit 1 touch. The device creates `remote_keyboard0` and `remote_touch0` for the bits in the mask
     and removes the others. Answered with `0x15` ok, or `0x16` fail `[message]`. All of them are removed when the session ends.
   - `0x41` key: `[codepoint u32][flags u8]` with bit 0 pressed, bit 1 ctrl and bit 2 alt. The codepoints are those of `KeyboardKeyData::key`.
   - `0x42` touch: `[count u8]` followed by `count` points `[x u16][y u16]`, at most 5, in the native orientation of the display. The device passes them on as they are.
     The frame lists all points that are touched at that moment and replaces the previous list, so a point that is no longer listed was released and a count of 0 releases everything.
     The order of the points is not significant: the touch device reports them, and LVGL follows each finger by its position.
   Input frames are ignored while their device doesn't exist.

8. In `info` mode, no client certificate is needed and the client isn't prompted for. The server answers with one JSON line and closes the connection:
   `{"status":"accepted","info":{"vendor":"LilyGO","name":"T-Deck","id":"lilygo-tdeck","version":"0.8.0"}}`
   The vendor is empty for devices without one.

`Buildscripts/TactilitySDK/Tools/shell.py` is a client for this server. App developers run it as `tt shell`, and copy files with `tt push` and `tt pull`. `tt remote` shows the display and sends input (`remote.py` and `_remote_viewer.py`).

License: [Apache v2.0](LICENSE-Apache-2.0.md)
