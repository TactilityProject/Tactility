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

7. In `info` mode, no client certificate is needed and the client isn't prompted for. The server answers with one JSON line and closes the connection:
   `{"status":"accepted","info":{"vendor":"LilyGO","name":"T-Deck","id":"lilygo-tdeck","version":"0.8.0"}}`
   The vendor is empty for devices without one.

`Buildscripts/TactilitySDK/Tools/shell.py` is a client for this server. App developers run it as `tt shell`, and copy files with `tt push` and `tt pull`.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
