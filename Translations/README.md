# Translations

Tactility uses gettext style translations: the English text in the source code is the key, and a missing translation shows the English text.

```cpp
#include <localisation/tr.h>

lv_label_set_text(label, tr("Delete"));
alertdialog::start(id, tr("Are you sure?"), tt::i18n::format(tr("Delete {}?"), { name }), buttons);
```

- Use `trc("context", "Open")` when one English word has more than one meaning.
- Put `{}` placeholders in the text instead of concatenating strings, so a translation can change the word order. Use `tt::i18n::format()` to fill them in.
- Don't translate text that is built from parts or that is a file name, id or log message.

## Files

`Translations/<domain>/<locale>.po` holds the translations of Tactility, for example `Translations/tactility/nl-NL.po`. The `tactility` domain is shared by every app. Each app of Tactility has its own domain, a folder named after it like `Translations/setup/`. A text that an app's domain lacks falls back to the `tactility` domain, so put texts that several apps use ("Brightness", "Never", colour names) there. They are standard PO files: edit them with a text editor or a tool like Poedit.
English needs no file. A region falls back to the file of its language (`nl.po` serves `nl-NL`).

The build compiles them with `Buildscripts/l10n/generate-catalogs.py` and embeds them in the firmware.

## Adding a language

1. Copy an existing file to `Translations/<domain>/<locale>.po`, for each domain and translate the `msgstr` values.
2. Add the language to the table in `Tactility/Source/settings/Language.cpp`.
3. Build. Fuzzy (`#, fuzzy`) and empty entries are skipped.

## Extracting texts

`python Buildscripts/TactilitySDK/Tools/extract_texts.py <source-folder>... > messages.pot` lists every `tr()`/`trc()` text, as a template for a new `.po` file.

## Apps with their own translations

See the localisation page in the Tactility documentation: apps ship `translations/<locale>.po` files that `tt app build` compiles, and `tt app translations` makes the template.

## Translating a built-in app

Define the folder name and include the domain header before anything that includes `<localisation/tr.h>`:

```cpp
#define L10N_APP "display"
#include <Tactility/i18n/AppDomain.h>
```

The domain is opened on first use. Use `tr_noop("text")` for texts in constant tables and `tr(table[i])` where they are shown.
