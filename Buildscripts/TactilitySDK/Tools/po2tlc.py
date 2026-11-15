#!/usr/bin/env python3
"""
Compiles a gettext PO file into a binary catalog (.tlc) for the localisation module.

Usage: po2tlc.py <input.po> <output.tlc>

The format is described in Modules/localisation-module/private/localisation/catalog.h.
This file is shared by tt app build and Buildscripts/l10n/generate-catalogs.py.
Entries that are fuzzy, untranslated or plural are skipped: they fall back to the English text.
"""
import struct
import sys

CONTEXT_SEPARATOR = "\x04"


def fnv1a(key: bytes) -> int:
    value = 2166136261
    for byte in key:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


SIMPLE_ESCAPES = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11, "\\": 92, '"': 34, "'": 39, "?": 63}


def decode_literal(body: str) -> bytes:
    """@return the bytes of the text between the quotes of a C or PO string, with its escape sequences decoded"""
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


def unquote(text: str) -> bytes:
    """@return the bytes of a quoted string. Fragments of a string on several lines are joined before they are decoded as UTF-8."""
    text = text.strip()
    if len(text) < 2 or text[0] != '"' or text[-1] != '"':
        raise ValueError(f"Expected a quoted string: {text}")
    return decode_literal(text[1:-1])


def parse_po(path: str) -> dict:
    """@return a dict of key (context + separator + text, or text) to translation"""
    entries = {}
    current = {}
    field = None
    fuzzy = False

    def finish():
        nonlocal current, field, fuzzy
        if "msgid" in current and "msgstr" in current and "msgid_plural" not in current:
            msgid = current["msgid"].decode("utf-8", errors="replace")
            msgstr = current["msgstr"].decode("utf-8", errors="replace")
            if msgid and msgstr and not fuzzy:
                key = current["msgctxt"].decode("utf-8", errors="replace") + CONTEXT_SEPARATOR + msgid if "msgctxt" in current else msgid
                entries[key] = msgstr
        current = {}
        field = None
        fuzzy = False

    def is_translated():
        return "msgstr" in current or "plural_msgstr" in current

    with open(path, "r", encoding="utf-8") as file:
        for number, raw_line in enumerate(file, 1):
            line = raw_line.strip()
            if not line:
                finish()
            elif line.startswith("#~"):
                continue
            elif line.startswith("#"):
                # Comments before an entry belong to it, so an entry that is complete ends first: a blank line is optional
                if is_translated():
                    finish()
                if line.startswith("#,") and "fuzzy" in line:
                    fuzzy = True
            elif line.startswith('"'):
                if field is None:
                    raise ValueError(f"{path}:{number}: string without a field")
                current[field] += unquote(line)
            else:
                name, _, value = line.partition(" ")
                if name.startswith("msgstr["):
                    name = "plural_msgstr"
                if name not in ("msgctxt", "msgid", "msgid_plural", "msgstr", "plural_msgstr"):
                    raise ValueError(f"{path}:{number}: unknown field {name}")
                # A new entry starts after a translated one, singular or plural. Its comments were handled above.
                if name in ("msgctxt", "msgid") and is_translated():
                    finish()
                if name == "msgctxt" and "msgid" in current:
                    finish()
                field = name
                if name == "plural_msgstr":
                    current[name] = current.get(name, b"") + unquote(value)
                else:
                    current[name] = unquote(value)
        finish()
    return entries


def build_catalog(entries: dict) -> bytes:
    items = sorted(((fnv1a(key.encode("utf-8")), key, value) for key, value in entries.items()), key=lambda item: (item[0], item[1]))
    blob = bytearray()
    table = bytearray()
    for hash_value, key, value in items:
        key_offset = len(blob)
        blob += key.encode("utf-8") + b"\0"
        value_offset = len(blob)
        blob += value.encode("utf-8") + b"\0"
        table += struct.pack("<III", hash_value, key_offset, value_offset)
    return b"TLC1" + struct.pack("<III", 1, len(items), 0) + bytes(table) + bytes(blob)


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.po> <output.tlc>")
        sys.exit(1)
    catalog = build_catalog(parse_po(sys.argv[1]))
    with open(sys.argv[2], "wb") as file:
        file.write(catalog)


if __name__ == "__main__":
    main()
