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


def unquote(text: str) -> str:
    text = text.strip()
    if len(text) < 2 or text[0] != '"' or text[-1] != '"':
        raise ValueError(f"Expected a quoted string: {text}")
    result = []
    index = 1
    end = len(text) - 1
    escapes = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
    while index < end:
        character = text[index]
        if character == "\\" and index + 1 < end:
            index += 1
            result.append(escapes.get(text[index], "\\" + text[index]))
        else:
            result.append(character)
        index += 1
    return "".join(result)


def parse_po(path: str) -> dict:
    """@return a dict of key (context + separator + text, or text) to translation"""
    entries = {}
    current = {}
    field = None
    fuzzy = False

    def finish():
        nonlocal current, field, fuzzy
        if "msgid" in current and "msgstr" in current and "msgid_plural" not in current:
            msgid = current["msgid"]
            msgstr = current["msgstr"]
            if msgid and msgstr and not fuzzy:
                key = current["msgctxt"] + CONTEXT_SEPARATOR + msgid if "msgctxt" in current else msgid
                entries[key] = msgstr
        current = {}
        field = None
        fuzzy = False

    with open(path, "r", encoding="utf-8") as file:
        for number, raw_line in enumerate(file, 1):
            line = raw_line.strip()
            if not line:
                finish()
            elif line.startswith("#,"):
                if "fuzzy" in line:
                    fuzzy = True
            elif line.startswith("#"):
                continue
            elif line.startswith('"'):
                if field is None:
                    raise ValueError(f"{path}:{number}: string without a field")
                current[field] += unquote(line)
            else:
                name, _, value = line.partition(" ")
                if name.startswith("msgstr["):
                    name = "msgid_plural"
                if name not in ("msgctxt", "msgid", "msgid_plural", "msgstr"):
                    raise ValueError(f"{path}:{number}: unknown field {name}")
                if name == "msgctxt" and "msgid" in current:
                    finish()
                if name == "msgid" and "msgstr" in current:
                    finish()
                field = name
                current[field] = unquote(value)
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
