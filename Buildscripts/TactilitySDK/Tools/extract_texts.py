#!/usr/bin/env python3
"""
Lists the texts of tr("...") and trc("...", "...") calls as a PO template (.pot).

Usage: extract_texts.py <source-folder-or-file>... > messages.pot

Only C and C++ files are scanned, without the build folders. A text must be string literals (adjacent literals are joined). Escape sequences are decoded, so the text is the key that the program uses.
This file is shared by tt app translations and the Tactility repository.
"""
import os
import re
import sys

EXTENSIONS = (".c", ".cc", ".cpp", ".h", ".hpp")
STRING = r'((?:"(?:[^"\\\n]|\\.)*"\s*)+)'
CALL = re.compile(r'\btr\s*\(\s*' + STRING + r'\)|\btrc\s*\(\s*' + STRING + r',\s*' + STRING + r'\)')


SIMPLE_ESCAPES = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11, "\\": 92, '"': 34, "'": 39, "?": 63}


def decode_literal(body: str) -> bytes:
    """@return the bytes of the C string literal with the text between the quotes, with its escape sequences decoded"""
    result = bytearray()
    index = 0
    while index < len(body):
        character = body[index]
        if character != "\\" or index + 1 >= len(body):
            result += character.encode("utf-8")
            index += 1
            continue
        index += 1
        escape = body[index]
        if escape in SIMPLE_ESCAPES:
            result.append(SIMPLE_ESCAPES[escape])
            index += 1
        elif escape in "01234567":
            end = index
            while end < len(body) and end < index + 3 and body[end] in "01234567":
                end += 1
            result.append(int(body[index:end], 8) & 0xFF)
            index = end
        elif escape == "x":
            end = index + 1
            while end < len(body) and body[end] in "0123456789abcdefABCDEF":
                end += 1
            result.append(int(body[index + 1:end] or "0", 16) & 0xFF)
            index = end
        elif escape in "uU":
            digits = 4 if escape == "u" else 8
            result += chr(int(body[index + 1:index + 1 + digits], 16)).encode("utf-8")
            index += 1 + digits
        else:
            result += ("\\" + escape).encode("utf-8")
            index += 1
    return bytes(result)


def join_literals(text: str) -> str:
    """Decodes each literal on its own, so an escape can't swallow the characters of the next literal, then joins them"""
    decoded = b"".join(decode_literal(body) for body in re.findall(r'"((?:[^"\\\n]|\\.)*)"', text))
    return decoded.decode("utf-8", errors="replace")


def escape_po(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")


def is_skipped_folder(name: str) -> bool:
    return name.startswith(".") or name == "build" or name.startswith("cmake-build")


def scan_file(path: str, entries: dict):
    with open(path, "r", encoding="utf-8", errors="replace") as file:
        source = file.read()
    for match in CALL.finditer(source):
        line = source.count("\n", 0, match.start()) + 1
        if match.group(1) is not None:
            key = (None, join_literals(match.group(1)))
        else:
            key = (join_literals(match.group(2)), join_literals(match.group(3)))
        entries.setdefault(key, []).append(f"{path}:{line}")


def extract(paths: list) -> str:
    """@return the content of a .pot file with the texts that were found in the files and folders"""
    entries = {}
    for argument in paths:
        if os.path.isfile(argument):
            scan_file(argument, entries)
            continue
        for directory, directories, file_names in os.walk(argument):
            directories[:] = sorted(name for name in directories if not is_skipped_folder(name))
            for file_name in sorted(file_names):
                if file_name.endswith(EXTENSIONS):
                    scan_file(os.path.join(directory, file_name), entries)

    lines = ['msgid ""', 'msgstr ""', '"Content-Type: text/plain; charset=UTF-8\\n"', ""]
    for (context, text), places in sorted(entries.items(), key=lambda item: (item[0][0] or "", item[0][1])):
        lines.append("#: " + " ".join(places))
        if context is not None:
            lines.append(f'msgctxt "{escape_po(context)}"')
        lines.append(f'msgid "{escape_po(text)}"')
        lines.append('msgstr ""')
        lines.append("")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <source-folder-or-file>...")
        sys.exit(1)
    sys.stdout.write(extract(sys.argv[1:]))


if __name__ == "__main__":
    main()
