"""Tests for the text extraction of tt app translations, up to the compiled catalog. Run with: python3 -m unittest discover -s Tests/Python -p '*_test.py'"""

import os
import sys
import tempfile
import unittest

TOOLS_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Buildscripts", "TactilitySDK", "Tools"))
sys.path.insert(0, TOOLS_PATH)

import extract_texts  # noqa: E402
import po2tlc  # noqa: E402


def translate(pot, value):
    """Gives every entry after the header a translation, as entries without one are not compiled"""
    header, separator, entries = pot.partition("\n\n")
    return header + separator + entries.replace('msgstr ""', f'msgstr "{value}"')


class ExtractTextsTest(unittest.TestCase):
    def extract_keys(self, source):
        """Extracts the texts of C source, gives each a translation and returns the keys that the compiled catalog has"""
        with tempfile.TemporaryDirectory() as directory:
            with open(os.path.join(directory, "main.c"), "w", encoding="utf-8") as file:
                file.write(source)
            pot = extract_texts.extract([directory])
            po_path = os.path.join(directory, "test.po")
            with open(po_path, "w", encoding="utf-8") as file:
                file.write(translate(pot, "x"))
            return set(po2tlc.parse_po(po_path))

    def test_plain_text_and_context(self):
        keys = self.extract_keys('void f() { tr("Hello"); trc("verb", "Open"); }')
        self.assertEqual(keys, {"Hello", "verb\x04Open"})

    def test_hex_escape_ends_before_the_next_literal(self):
        # "\x44" is D. The e of the next literal is a hex digit, but it is not part of the escape
        self.assertEqual(self.extract_keys('void f() { tr("\\x44" "elete"); }'), {"Delete"})

    def test_escapes_are_decoded(self):
        keys = self.extract_keys('void f() { tr("Line\\none"); tr("Say \\"hi\\""); tr("Tab\\there"); tr("Back\\\\slash"); tr("\\101\\u00e9"); }')
        self.assertEqual(keys, {"Line\none", 'Say "hi"', "Tab\there", "Back\\slash", "Aé"})

    def test_utf8_bytes_from_hex_escapes(self):
        self.assertEqual(self.extract_keys('void f() { tr("caf\\xc3\\xa9"); }'), {"café"})

    def test_compiled_catalog_holds_the_runtime_key(self):
        with tempfile.TemporaryDirectory() as directory:
            with open(os.path.join(directory, "main.c"), "w") as file:
                file.write('void f() { tr("\\x44" "elete"); }')
            po_path = os.path.join(directory, "test.po")
            with open(po_path, "w") as file:
                file.write(translate(extract_texts.extract([directory]), "Verwijderen"))
            catalog = po2tlc.build_catalog(po2tlc.parse_po(po_path))
        # The catalog stores key and value as NUL terminated strings: the key is what the program looks up
        self.assertIn(b"Delete\0Verwijderen\0", catalog)

    def test_build_folders_are_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            os.makedirs(os.path.join(directory, "build"))
            with open(os.path.join(directory, "build", "x.c"), "w") as file:
                file.write('tr("Ignored");')
            self.assertNotIn("Ignored", extract_texts.extract([directory]))


if __name__ == "__main__":
    unittest.main()
