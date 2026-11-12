#!/usr/bin/env python3
# License: Apache License v2
"""Client for the Tactility development server (Modules/development-module)."""

import argparse
import datetime
import getpass
import hashlib
import json
import os
import select
import signal
import socket
import ssl
import struct
import sys
import tempfile
import threading
import time

import _device_host

DEFAULT_PORT = 6667
PROTOCOL_VERSION = 1
CONFIG_DIRECTORY = os.path.join(os.path.expanduser("~"), ".tactility")
CLIENT_KEY_PATH = os.path.join(CONFIG_DIRECTORY, "client.key")
CLIENT_CERTIFICATE_PATH = os.path.join(CONFIG_DIRECTORY, "client.crt")
KNOWN_HOSTS_PATH = os.path.join(CONFIG_DIRECTORY, "known_hosts")
CONNECT_TIMEOUT_SECONDS = 10
# Leaves time for the user to answer the prompt on the device
ACCEPT_TIMEOUT_SECONDS = 90
PROMPT_HINT_DELAY_SECONDS = 1
# How long the device may take to answer a request, and to answer an install, which extracts the whole package first
REQUEST_TIMEOUT_SECONDS = 10
INSTALL_TIMEOUT_SECONDS = 120

# tt runs this tool as "tt shell"
PROGRAM_NAME = "tt shell" if "TACTILITY_SDK_VERSION" in os.environ else "shell.py"

FRAME_DATA = 0x01
FRAME_RESIZE = 0x02
FRAME_EXIT = 0x03
FRAME_SYNC_STAT = 0x10
FRAME_SYNC_STAT_RESULT = 0x11
FRAME_SYNC_PUSH = 0x12
FRAME_SYNC_PULL = 0x13
FRAME_DONE = 0x14
FRAME_OK = 0x15
FRAME_FAIL = 0x16
FRAME_APP_INSTALL = 0x20
FRAME_APP_UNINSTALL = 0x21
FRAME_APP_RUN = 0x22
SYNC_FILE_MISSING = 0
SYNC_FILE_DIRECTORY = 2
SYNC_READ_SIZE = 64 * 1024
FRAME_HEADER_SIZE = 3
COMMAND_MAX = 511
FRAME_PAYLOAD_MAX = 1024


def fail(message):
    print(f"{PROGRAM_NAME}: {message}", file=sys.stderr)
    sys.exit(1)


HIGHLIGHT_START = "\033[38;5;117m"
HIGHLIGHT_END = "\033[0m"


def use_color(stream):
    return stream.isatty() and "NO_COLOR" not in os.environ


def format_fingerprint(fingerprint, stream=sys.stdout):
    """Groups the fingerprint in words of 4 and highlights the first 2 words, which the device shows too"""
    words = [fingerprint[i:i + 4] for i in range(0, len(fingerprint), 4)]
    if use_color(stream) and len(words) > 2:
        return f"{HIGHLIGHT_START}{' '.join(words[:2])}{HIGHLIGHT_END} {' '.join(words[2:])}"
    return " ".join(words)


def certificate_fingerprint(der):
    return hashlib.sha256(der).hexdigest()


# region Identity

def ensure_client_identity():
    """Creates the self-signed client certificate that identifies this computer to devices"""
    if os.path.isfile(CLIENT_KEY_PATH) and os.path.isfile(CLIENT_CERTIFICATE_PATH):
        return
    try:
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.x509.oid import NameOID
    except ImportError:
        fail("the 'cryptography' package is required: pip install cryptography")

    os.makedirs(CONFIG_DIRECTORY, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, client_name())])
    now = datetime.datetime.now(datetime.timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=365 * 50))
        .sign(key, hashes.SHA256())
    )
    key_bytes = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    descriptor = os.open(CLIENT_KEY_PATH, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "wb") as file:
        file.write(key_bytes)
    with open(CLIENT_CERTIFICATE_PATH, "wb") as file:
        file.write(certificate.public_bytes(serialization.Encoding.PEM))
    print(f"Created client certificate in {CONFIG_DIRECTORY}")


def client_name():
    return f"{getpass.getuser()}@{socket.gethostname()}"


def client_fingerprint():
    with open(CLIENT_CERTIFICATE_PATH, "rb") as file:
        der = ssl.PEM_cert_to_DER_cert(file.read().decode("ascii"))
    return certificate_fingerprint(der)

# endregion

# region Known hosts

def read_known_hosts():
    hosts = {}
    if os.path.isfile(KNOWN_HOSTS_PATH):
        with open(KNOWN_HOSTS_PATH) as file:
            for line in file:
                parts = line.split()
                if len(parts) == 2:
                    hosts[parts[0]] = parts[1]
    return hosts


def write_known_host(address, fingerprint):
    """Adds the device, or replaces its fingerprint"""
    hosts = read_known_hosts()
    hosts[address] = fingerprint
    os.makedirs(CONFIG_DIRECTORY, exist_ok=True)
    with open(KNOWN_HOSTS_PATH, "w") as file:
        for known_address, known_fingerprint in hosts.items():
            file.write(f"{known_address} {known_fingerprint}\n")


def ask_to_continue(question, not_interactive_message):
    try:
        answer = input(question).strip().lower()
    except EOFError:
        print(flush=True)
        fail(not_interactive_message)
    if answer not in ("y", "yes"):
        fail("aborted")


def verify_device(host, port, fingerprint):
    """Trust on first use: remembers the device fingerprint. A device that changed it is only accepted when the user confirms.
    @return True when the user was asked to trust the device"""
    address = f"{host}:{port}"
    known = read_known_hosts().get(address)
    if known == fingerprint:
        return False
    if known is not None:
        print(
            f"WARNING: the fingerprint of {address} changed!\n"
            f"  expected: {format_fingerprint(known, sys.stderr)}\n"
            f"  received: {format_fingerprint(fingerprint, sys.stderr)}\n"
            f"This could mean that someone intercepts the connection, or that the device was reset.",
            file=sys.stderr
        )
        ask_to_continue(
            "Accept the new fingerprint and continue connecting? (y/N) ",
            f"the fingerprint of {address} changed: run 'tt shell' in a terminal to verify and accept it"
        )
        write_known_host(address, fingerprint)
        return True
    print(f"The authenticity of {address} can't be established.")
    print(f"Device fingerprint: {format_fingerprint(fingerprint)}")
    print("Compare it with the fingerprint in the Development app on the device.")
    ask_to_continue(
        "Continue connecting? (y/N) ",
        f"{address} is not a known device yet: run 'tt shell' in a terminal once to verify and trust its fingerprint"
    )
    write_known_host(address, fingerprint)
    return True

# endregion

# region Connection

# A device that disappears without closing the connection (power loss, Wi-Fi drop) is noticed after
# about KEEPALIVE_IDLE_SECONDS + KEEPALIVE_INTERVAL_SECONDS * KEEPALIVE_PROBES when the connection is idle,
# and after USER_TIMEOUT_SECONDS when sent data isn't acknowledged
KEEPALIVE_IDLE_SECONDS = 5
KEEPALIVE_INTERVAL_SECONDS = 3
KEEPALIVE_PROBES = 3
USER_TIMEOUT_SECONDS = 15


def enable_keepalive(raw_socket):
    raw_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    if os.name == "nt":
        raw_socket.ioctl(socket.SIO_KEEPALIVE_VALS, (1, KEEPALIVE_IDLE_SECONDS * 1000, KEEPALIVE_INTERVAL_SECONDS * 1000))
        return
    # Linux names the idle time TCP_KEEPIDLE, macOS TCP_KEEPALIVE
    idle_option = getattr(socket, "TCP_KEEPIDLE", None) or getattr(socket, "TCP_KEEPALIVE", None)
    options = [
        (idle_option, KEEPALIVE_IDLE_SECONDS),
        (getattr(socket, "TCP_KEEPINTVL", None), KEEPALIVE_INTERVAL_SECONDS),
        (getattr(socket, "TCP_KEEPCNT", None), KEEPALIVE_PROBES),
        (getattr(socket, "TCP_USER_TIMEOUT", None), USER_TIMEOUT_SECONDS * 1000),
    ]
    for option, value in options:
        if option is not None:
            try:
                raw_socket.setsockopt(socket.IPPROTO_TCP, option, value)
            except OSError:
                pass


class Connection:

    def __init__(self, host, port):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        # Devices have self-signed certificates: verify_device() pins them instead
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.load_cert_chain(CLIENT_CERTIFICATE_PATH, CLIENT_KEY_PATH)
        try:
            raw_socket = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT_SECONDS)
            enable_keepalive(raw_socket)
            self.socket = context.wrap_socket(raw_socket)
        except (OSError, ssl.SSLError) as error:
            fail(f"failed to connect to {host}:{port}: {error}")
        self.buffer = b""
        self.fingerprint = certificate_fingerprint(self.socket.getpeercert(binary_form=True))

    def send(self, data):
        try:
            self.socket.sendall(data)
        except (OSError, ssl.SSLError):
            fail("the device closed the connection")

    def receive(self):
        """@return False when the connection closed. Raises socket.timeout when the socket timeout passes."""
        try:
            data = self.socket.recv(4096)
        except socket.timeout:
            raise
        except (OSError, ssl.SSLError):
            return False
        if not data:
            return False
        self.buffer += data
        return True

    def read_line(self, timeout):
        self.socket.settimeout(timeout)
        try:
            while b"\n" not in self.buffer:
                if not self.receive():
                    return None
        except socket.timeout:
            return None
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode("utf-8", errors="replace")

    def send_frame(self, frame_type, payload=b""):
        for offset in range(0, max(len(payload), 1), FRAME_PAYLOAD_MAX):
            chunk = payload[offset:offset + FRAME_PAYLOAD_MAX]
            self.send(struct.pack(">BH", frame_type, len(chunk)) + chunk)

    def pop_frame(self):
        """@return (type, payload) or None when no complete frame was received yet"""
        if len(self.buffer) < FRAME_HEADER_SIZE:
            return None
        frame_type, length = struct.unpack(">BH", self.buffer[:FRAME_HEADER_SIZE])
        if len(self.buffer) < FRAME_HEADER_SIZE + length:
            return None
        payload = self.buffer[FRAME_HEADER_SIZE:FRAME_HEADER_SIZE + length]
        self.buffer = self.buffer[FRAME_HEADER_SIZE + length:]
        return frame_type, payload

    def read_frame(self, timeout=REQUEST_TIMEOUT_SECONDS):
        """Waits for a frame. @return (type, payload)"""
        self.socket.settimeout(timeout)
        try:
            while (frame := self.pop_frame()) is None:
                if not self.receive():
                    fail("the device closed the connection")
        except socket.timeout:
            fail(f"the device did not answer within {timeout} seconds")
        return frame

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass


def open_session(host, port, mode, columns=80, rows=24, command=None):
    ensure_client_identity()
    connection = Connection(host, port)
    if verify_device(host, port, connection.fingerprint):
        # The device closes a connection that doesn't greet it in time, which answering the question can take longer than
        connection.close()
        connection = Connection(host, port)
        verify_device(host, port, connection.fingerprint)

    hello = {"protocol": PROTOCOL_VERSION, "mode": mode, "client": client_name(), "columns": columns, "rows": rows}
    if command is not None:
        hello["command"] = command
    connection.send((json.dumps(hello, separators=(",", ":")) + "\n").encode("utf-8"))

    # A trusted client is answered right away, otherwise the device asks the user
    line = connection.read_line(PROMPT_HINT_DELAY_SECONDS)
    if line is None:
        print(f"Client fingerprint: {format_fingerprint(client_fingerprint())}", flush=True)
        print("Waiting for the device to accept the connection...", flush=True)
        line = connection.read_line(ACCEPT_TIMEOUT_SECONDS)
    if line is None:
        fail("the device closed the connection or did not answer")
    try:
        status = json.loads(line)
    except json.JSONDecodeError:
        fail(f"unexpected answer: {line}")
    if status.get("status") == "rejected":
        fail("the device rejected the connection")
    if status.get("status") != "accepted":
        fail(f"the device refused the connection: {status.get('message', line)}")
    connection.socket.settimeout(REQUEST_TIMEOUT_SECONDS)
    return connection

# endregion

# region Shell

def terminal_size():
    try:
        size = os.get_terminal_size(sys.stdout.fileno())
        return size.columns, size.lines
    except OSError:
        return 80, 24


def write_output(data, previous_was_cr):
    """The device shell emits bare line feeds, which a raw terminal doesn't return the cursor for"""
    output = bytearray()
    for byte in data:
        if byte == 0x0A and not previous_was_cr:
            output += b"\r"
        output.append(byte)
        previous_was_cr = byte == 0x0D
    sys.stdout.buffer.write(bytes(output))
    sys.stdout.buffer.flush()
    return previous_was_cr


class RawTerminal:
    """Sends every key to the device instead of letting the terminal edit lines, and lets it show escape sequences"""

    def __init__(self, enabled):
        self.enabled = enabled
        self.restore = None

    def __enter__(self):
        if self.enabled:
            self.restore = enter_windows_raw_mode() if os.name == "nt" else enter_posix_raw_mode()
        return self

    def __exit__(self, *_):
        if self.restore is not None:
            self.restore()


def enter_posix_raw_mode():
    import termios
    import tty
    stdin = sys.stdin.fileno()
    saved_attributes = termios.tcgetattr(stdin)
    tty.setraw(stdin)
    return lambda: termios.tcsetattr(stdin, termios.TCSADRAIN, saved_attributes)


def enter_windows_raw_mode():
    import ctypes
    kernel32 = ctypes.windll.kernel32
    enable_processed_input, enable_line_input, enable_echo_input = 0x0001, 0x0002, 0x0004
    enable_virtual_terminal_input = 0x0200
    enable_virtual_terminal_processing = 0x0004
    utf8_code_page = 65001
    input_handle = kernel32.GetStdHandle(-10)
    output_handle = kernel32.GetStdHandle(-11)
    input_mode = ctypes.c_uint32()
    output_mode = ctypes.c_uint32()
    kernel32.GetConsoleMode(input_handle, ctypes.byref(input_mode))
    kernel32.GetConsoleMode(output_handle, ctypes.byref(output_mode))
    input_code_page = kernel32.GetConsoleCP()
    # Keys arrive as the same escape sequences as on Linux and macOS, Ctrl+C included
    raw_input_mode = (input_mode.value & ~(enable_processed_input | enable_line_input | enable_echo_input)) | enable_virtual_terminal_input
    kernel32.SetConsoleMode(input_handle, raw_input_mode)
    kernel32.SetConsoleMode(output_handle, output_mode.value | enable_virtual_terminal_processing)
    kernel32.SetConsoleCP(utf8_code_page)

    def restore():
        kernel32.SetConsoleMode(input_handle, input_mode.value)
        kernel32.SetConsoleMode(output_handle, output_mode.value)
        kernel32.SetConsoleCP(input_code_page)
    return restore


def start_input_reader(wake_socket):
    """Reads stdin on a thread, because select() can't wait for it on Windows. None in the queue means end of input."""
    import queue
    inputs = queue.Queue()

    def read_input():
        stdin = sys.stdin.fileno()
        while True:
            try:
                data = os.read(stdin, FRAME_PAYLOAD_MAX)
            except OSError:
                data = b""
            inputs.put(data or None)
            try:
                wake_socket.send(b"i")
            except BlockingIOError:
                pass
            except OSError:
                return
            if not data:
                return

    threading.Thread(target=read_input, daemon=True).start()
    return inputs


def run_shell(args):
    """Opens an interactive shell, or runs a command like ssh does when one is given"""
    command = " ".join(args.command) if args.command else None
    if command is not None and len(command.encode("utf-8")) > COMMAND_MAX:
        fail(f"the command is longer than {COMMAND_MAX} bytes")
    columns, rows = terminal_size()
    connection = open_session(args.host, args.port, "shell", columns, rows, command)
    # A shell or command can be silent for as long as it likes
    connection.socket.settimeout(None)

    # Input and resizes wake up the select() below through this socket pair
    wake_read, wake_write = socket.socketpair()
    # A full buffer already wakes up the loop, and a signal handler must never block
    wake_write.setblocking(False)
    has_resize_signal = hasattr(signal, "SIGWINCH")
    previous_resize_handler = None
    if has_resize_signal:
        def on_resize(*_):
            try:
                wake_write.send(b"r")
            except OSError:
                pass
        previous_resize_handler = signal.signal(signal.SIGWINCH, on_resize)
    # Without a resize signal, the window size is checked this often
    resize_poll_seconds = None if has_resize_signal else 0.5
    window_size = (columns, rows)

    # A command gets its input as is, an interactive shell gets every key
    is_terminal = os.isatty(sys.stdin.fileno()) and command is None
    exit_code = None
    previous_was_cr = False
    try:
        with RawTerminal(is_terminal):
            inputs = start_input_reader(wake_write)
            while exit_code is None:
                # Already decrypted data doesn't make the socket readable
                timeout = 0 if connection.socket.pending() > 0 else resize_poll_seconds
                readable, _, _ = select.select([connection.socket, wake_read], [], [], timeout)

                if wake_read in readable:
                    wake_read.recv(256)
                while not inputs.empty():
                    data = inputs.get()
                    if data is not None:
                        connection.send_frame(FRAME_DATA, data)
                if terminal_size() != window_size:
                    window_size = terminal_size()
                    connection.send_frame(FRAME_RESIZE, struct.pack(">HH", *window_size))
                if connection.socket in readable or connection.socket.pending() > 0:
                    if not connection.receive():
                        break
                    while (frame := connection.pop_frame()) is not None:
                        frame_type, payload = frame
                        if frame_type == FRAME_DATA:
                            previous_was_cr = write_output(payload, previous_was_cr)
                        elif frame_type == FRAME_EXIT and len(payload) == 4:
                            exit_code = struct.unpack(">i", payload)[0]
    finally:
        if has_resize_signal:
            signal.signal(signal.SIGWINCH, previous_resize_handler)
        connection.close()
        wake_write.close()
        wake_read.close()

    if exit_code is None:
        print("\nConnection closed", file=sys.stderr)
        return 1
    return exit_code


def run_command(host, port, command, timeout=REQUEST_TIMEOUT_SECONDS):
    """Runs a command without a terminal. @return (exit code, output text)"""
    connection = open_session(host, port, "shell", command=command)
    output = bytearray()
    try:
        while True:
            frame_type, payload = connection.read_frame(timeout)
            if frame_type == FRAME_DATA:
                output += payload
            elif frame_type == FRAME_EXIT and len(payload) == 4:
                return struct.unpack(">i", payload)[0], output.decode("utf-8", errors="replace").replace("\r", "")
    finally:
        connection.close()

# endregion


# region Sync

def check_remote_path(path):
    if not path.startswith("/"):
        fail(f"remote path must be absolute: {path}")


def remote_stat(connection, path):
    """@return (file type, size)"""
    connection.send_frame(FRAME_SYNC_STAT, path.encode("utf-8"))
    frame_type, payload = connection.read_frame()
    if frame_type == FRAME_FAIL:
        fail(f"{path}: {payload.decode('utf-8', errors='replace')}")
    if frame_type != FRAME_SYNC_STAT_RESULT or len(payload) != 9:
        fail("unexpected answer from the device")
    return payload[0], struct.unpack(">Q", payload[1:])[0]


def send_file(connection, request_type, name, local_path):
    """Sends a request that is followed by the file contents. @return the file size"""
    size = 0
    connection.send_frame(request_type, name.encode("utf-8"))
    with open(local_path, "rb") as file:
        while data := file.read(SYNC_READ_SIZE):
            connection.send_frame(FRAME_DATA, data)
            size += len(data)
    connection.send_frame(FRAME_DONE)
    return size


def expect_ok(connection, subject, timeout=REQUEST_TIMEOUT_SECONDS):
    frame_type, payload = connection.read_frame(timeout)
    if frame_type == FRAME_FAIL:
        fail(f"{subject}: {payload.decode('utf-8', errors='replace')}")
    if frame_type != FRAME_OK:
        fail("unexpected answer from the device")


def print_transfer(description, size, start):
    duration = max(time.monotonic() - start, 0.001)
    print(f"{description}: {size / duration / 1024:.1f} KB/s ({size} bytes in {duration:.3f}s)")


def run_push(args):
    check_remote_path(args.remote_path)
    if os.path.isdir(args.local_path):
        fail(f"{args.local_path}: directories are not supported")
    if not os.path.isfile(args.local_path):
        fail(f"{args.local_path}: no such file")

    connection = open_session(args.host, args.port, "sync")
    remote_path = args.remote_path
    file_type, _ = remote_stat(connection, remote_path)
    if file_type == SYNC_FILE_DIRECTORY or remote_path.endswith("/"):
        remote_path = remote_path.rstrip("/") + "/" + os.path.basename(args.local_path)

    start = time.monotonic()
    size = send_file(connection, FRAME_SYNC_PUSH, remote_path, args.local_path)
    expect_ok(connection, remote_path)
    connection.close()
    print_transfer(f"{args.local_path} -> {remote_path}", size, start)
    return 0


def run_pull(args):
    check_remote_path(args.remote_path)
    connection = open_session(args.host, args.port, "sync")
    file_type, _ = remote_stat(connection, args.remote_path)
    if file_type == SYNC_FILE_MISSING:
        fail(f"{args.remote_path}: no such file")
    if file_type == SYNC_FILE_DIRECTORY:
        fail(f"{args.remote_path}: directories are not supported")

    local_path = args.local_path
    if os.path.isdir(local_path):
        local_path = os.path.join(local_path, os.path.basename(args.remote_path))

    start = time.monotonic()
    size = 0
    connection.send_frame(FRAME_SYNC_PULL, args.remote_path.encode("utf-8"))
    # Leaves an existing file untouched when the transfer fails
    descriptor, temporary_path = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(local_path)), prefix=".tt-pull-")
    try:
        with os.fdopen(descriptor, "wb") as file:
            while True:
                frame_type, payload = connection.read_frame()
                if frame_type == FRAME_DATA:
                    file.write(payload)
                    size += len(payload)
                elif frame_type == FRAME_DONE:
                    break
                elif frame_type == FRAME_FAIL:
                    fail(f"{args.remote_path}: {payload.decode('utf-8', errors='replace')}")
                else:
                    fail("unexpected answer from the device")
        os.replace(temporary_path, local_path)
    finally:
        if os.path.exists(temporary_path):
            os.remove(temporary_path)
        connection.close()
    print_transfer(f"{args.remote_path} -> {local_path}", size, start)
    return 0

# endregion


# region App

def install(host, port, package_path):
    """Installs an app package (.app file) on the device. Exits on failure."""
    if not os.path.isfile(package_path):
        fail(f"{package_path}: no such file")
    connection = open_session(host, port, "app")
    try:
        send_file(connection, FRAME_APP_INSTALL, os.path.basename(package_path), package_path)
        expect_ok(connection, package_path, INSTALL_TIMEOUT_SECONDS)
    finally:
        connection.close()
    return True


def app_request(host, port, request_type, app_id):
    connection = open_session(host, port, "app")
    try:
        connection.send_frame(request_type, app_id.encode("utf-8"))
        expect_ok(connection, app_id)
    finally:
        connection.close()
    return True


def uninstall(host, port, package_id):
    """Uninstalls a package from the device. Succeeds when it wasn't installed. Exits on failure."""
    return app_request(host, port, FRAME_APP_UNINSTALL, package_id)


def run(host, port, app_id):
    """Starts an app on the device, or restarts it when it's in the foreground. Exits on failure."""
    return app_request(host, port, FRAME_APP_RUN, app_id)


def add_connection_arguments(parser):
    """Adds --help, --host and --port: -h is the host, so the help can't use it"""
    parser.add_argument("--help", action="help", help="show this help message and exit")
    parser.add_argument("-h", "--host", help="device hostname or IP address (default: the host of 'tt host')")
    parser.add_argument("-p", "--port", type=int, default=DEFAULT_PORT, help=f"device port (default: {DEFAULT_PORT})")


def parse_arguments(parser):
    """Parses the arguments of add_connection_arguments(), using the stored device host when --host is missing"""
    args = parser.parse_args()
    if args.host is None:
        args.host = _device_host.read_device_host()
    if args.host is None:
        fail(_device_host.MISSING_HOST_MESSAGE)
    return args


def add_push_arguments(parser):
    parser.add_argument("local_path", help="file on this computer")
    parser.add_argument("remote_path", help="absolute file or directory path on the device")


def add_pull_arguments(parser):
    parser.add_argument("remote_path", help="absolute file path on the device")
    parser.add_argument("local_path", nargs="?", default=".", help="file or directory on this computer (default: current directory)")


def main():
    parser = argparse.ArgumentParser(prog=PROGRAM_NAME, description="open a shell on the device, or run a command in it", add_help=False)
    add_connection_arguments(parser)
    parser.add_argument("command", nargs=argparse.REMAINDER, help="command to run instead of an interactive shell")
    sys.exit(run_shell(parse_arguments(parser)))


if __name__ == "__main__":
    import _bootstrap
    _bootstrap.run()
    main()
