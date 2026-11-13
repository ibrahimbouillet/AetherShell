# Interface translations (D70)

Aether Shell's interface is written in English in the code and translated through one catalog into French, Spanish,
Chinese (Simplified), Hindi (D70), Portuguese, Russian, Arabic, Bengali and Urdu (D74).

- `catalog/*.txt` — the translations, one block per text: `en:` (the exact English text in the code), then `fr:`,
  `es:`, `zh:`, `hi:`, `pt:`, `ru:`, `ar:`, `bn:`, `ur:`. `\n` is a line break. Blocks are separated by a blank line; `#`
  starts a comment line. In the languages whose script has no Latin letters for an access key (`zh`, `ar`, `bn`, `ur`),
  the key is written after the text, as Windows does: `连接(&C)`, `اتصال (&C)`.
- Adding a language: a column in `LANGUAGES` (`make_catalog.py`), a value in `core/Localization`'s `Language`, its row in
  `kLanguages` and `FromLocaleName`, `settings::InterfaceLanguage` and its name in `SettingsSerialization.cpp`, then the
  translation of every block.
- `make_catalog.py` — checks the catalog (duplicates, `{}` placeholders and `&` access keys identical to the English)
  and writes `src/core/Translations.inc`, which the build compiles. Run it after any change:

  ```powershell
  python tools/i18n/make_catalog.py --check
  python tools/i18n/make_catalog.py
  ```

- `extract_strings.py` — lists the wide string literals of C++ files that read as interface text, to find what a new
  feature still needs in the catalog.

In the code, interface text goes through `i18n::Tr(L"...")`, or `i18n::TrFormat(L"... {} ...", value)` for a sentence
with values; the Settings form, menus, dialogs and setup translate their labels where they create them. Names that
belong to the person or to other programs are never translated. A text missing from the catalog shows in English.
