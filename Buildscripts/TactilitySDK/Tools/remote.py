#!/usr/bin/env python3
# License: Apache License v2
"""Shows the display of the device and sends keyboard, touch and mouse input to it. Run it as "tt remote"."""

import argparse
import os

import shell
import _remote_viewer

# How long the device may take to start the session after it accepted the connection
START_TIMEOUT_SECONDS = 10


def main():
    shell.PROGRAM_NAME = "tt remote" if "TACTILITY_SDK_VERSION" in os.environ else "remote.py"
    parser = argparse.ArgumentParser(prog=shell.PROGRAM_NAME, description="show the display of the device and control it", add_help=False)
    shell.add_connection_arguments(parser)
    parser.add_argument("--appearance", choices=("dark", "light", "system"), default="system",
                        help="window theme (default: system)")
    parser.add_argument("--title", help="window title (default: program name and host)")
    args = shell.parse_arguments(parser)

    connection = shell.open_session(args.host, args.port, "remote")
    frame_type, payload = connection.read_frame(START_TIMEOUT_SECONDS)
    if frame_type == shell.FRAME_FAIL:
        shell.fail(payload.decode("utf-8", errors="replace"))
    state = _remote_viewer.parse_state(payload) if frame_type == shell.FRAME_REMOTE_INFO else None
    if state is None:
        shell.fail("the device did not describe its display")

    session = _remote_viewer.RemoteSession(connection)
    title = args.title or f"{shell.PROGRAM_NAME} {args.host}"
    _remote_viewer.run_viewer(session, state, title, args.appearance)


if __name__ == "__main__":
    import _bootstrap
    _bootstrap.run()
    main()
