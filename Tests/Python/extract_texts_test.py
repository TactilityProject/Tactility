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

    def test_comments_between_literals_and_arguments(self):
        keys = self.extract_keys('void f() { tr("Hello " /* explanation */ "world"); trc("verb" /* a */, // b\n "Open" /* c */ "ed"); }')
        self.assertEqual(keys, {"Hello world", "verb\x04Opened"})

    def test_comment_markers_inside_literals_are_kept(self):
        keys = self.extract_keys('void f() { tr("Visit http://x /* y */"); /* tr("Commented out"); */ // tr("Also");\n }')
        self.assertEqual(keys, {"Visit http://x /* y */"})

    def test_tr_noop_texts_are_found(self):
        self.assertEqual(self.extract_keys('static const char* NAMES[] = { tr_noop("Red"), tr_noop("Deep " "blue") };'), {"Red", "Deep blue"})

    def test_build_folders_are_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            os.makedirs(os.path.join(directory, "build"))
            with open(os.path.join(directory, "build", "x.c"), "w") as file:
                file.write('tr("Ignored");')
            self.assertNotIn("Ignored", extract_texts.extract([directory]))


class PoParseTest(unittest.TestCase):
    def parse(self, content):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "test.po")
            with open(path, "w", encoding="utf-8") as file:
                file.write(content)
            return po2tlc.parse_po(path)

    def test_numeric_and_hex_escapes_are_decoded(self):
        entries = self.parse('msgid "Save \\046 Close"\nmsgstr "Opslaan \\x26 sluiten"\n')
        self.assertEqual(entries, {"Save & Close": "Opslaan & sluiten"})

    def test_utf8_bytes_from_escapes(self):
        self.assertEqual(self.parse('msgid "Cafe"\nmsgstr "caf\\303\\251"\n'), {"Cafe": "café"})

    def test_fuzzy_flag_belongs_to_the_next_entry(self):
        # No blank line between the entries
        entries = self.parse('msgid "A"\nmsgstr "a"\n#, fuzzy\nmsgid "B"\nmsgstr "b"\n')
        self.assertEqual(entries, {"A": "a"})

    def test_singular_entry_after_a_plural_entry(self):
        entries = self.parse('msgid "One"\nmsgid_plural "Many"\nmsgstr[0] "een"\nmsgstr[1] "veel"\nmsgid "Next"\nmsgstr "volgende"\n')
        self.assertEqual(entries, {"Next": "volgende"})

    def test_utf8_sequence_split_across_lines(self):
        entries = self.parse('msgid "caf"\n"\\303"\n"\\251"\nmsgstr "koffie"\n')
        self.assertEqual(entries, {"café": "koffie"})

    def test_context_entry_after_an_entry(self):
        entries = self.parse('msgid "A"\nmsgstr "a"\nmsgctxt "verb"\nmsgid "B"\nmsgstr "b"\n')
        self.assertEqual(entries, {"A": "a", "verb\x04B": "b"})

if __name__ == "__main__":
    unittest.main()
