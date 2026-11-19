# Interface languages (D70, D74, D83)

Aether Shell's interface is written in English in the code. It is translated into the 38 languages Windows 11 offers
at download, plus Bengali, Hindi and Urdu: 41 languages in all, English included.

## Files

- `english/*.txt`: every interface text, as the code writes it, in blocks `en: <text>` separated by blank lines.
  `#` starts a comment that describes the next texts (it is shown to translators). One file per area: `app`, `shell`,
  `settings-pages`, `settings-messages`, `setup`, `weather`, `search-names`.
- `languages/<code>.txt`: one language. Each block repeats the English and gives the translation:

  ```text
  ## settings-pages
  en: Icon size
  tr: Taille des icônes
  ```

  A variant (`en-GB`, `es-MX`, `fr-CA`, `pt-PT`) lists only the texts that differ from its base language
  (English, `es`, `fr`, `pt-BR`); the rest is its base's. `\n` is a line break, `\t` a tab, `\\` a backslash.
  Every `{}` placeholder of the English must be kept. A text with an `&` access key keeps exactly one `&`. In scripts
  without Latin letters, the key follows the text as Windows writes it: `ダウンロード(&D)`.
- `languages.py`: the tool below. `extract_strings.py` lists the wide string literals of C++ sources that read as
  interface text, as a starting point when new texts are added.

## Where languages live

Only English is built into the programs. Every other language is a pack, `AetherShell-language-<code>.json`. Each
pack is attached to the version's GitHub release and downloaded when it is needed:
- when a person chooses the language in Settings > Language, which shows each language's size;
- for every language Windows has, by setup and at each start of the application, so Aether changes language when
  Windows does.

Each pack's size and SHA-256 are compiled into the programs (`src/core/LanguageTable.inc`). A pack is used only if it
is exactly the one this version was built with. Setup's own texts (`english/setup.txt`) are built into the setup
program in every language (`src/core/SetupTexts.inc`), because setup runs before anything can be downloaded.

## Commands

```powershell
python tools/i18n/languages.py list [--from N] [--to N]   # numbered English texts, for translating
python tools/i18n/languages.py show <code> [--from N] [--to N]
python tools/i18n/languages.py import <code> <work file> [--partial]
python tools/i18n/languages.py check [<code>...]          # non-zero exit on a problem
python tools/i18n/languages.py status                     # how complete each language is
python tools/i18n/languages.py build [--packs DIR]        # packs, LanguageTable.inc and SetupTexts.inc
python tools/i18n/languages.py packs DIR                  # only the packs (tests, package.ps1)
python tools/i18n/languages.py verify DIR                 # DIR holds exactly the packs LanguageTable.inc describes
```

A work file has one line per text, `N: translation`, with N from `list`; `#` comments and blank lines are ignored.
`--partial` keeps the texts already in the language file that the work file does not give.

## Adding or changing a text

1. Write the English in the code (through `i18n::Tr` or a `Tr*` helper) and add its block to the right
   `english/*.txt` file, with a comment when the context is not obvious.
2. Translate it in every language (`list`, then `import ... --partial` per language), then `check`.
3. Run `build`, which rewrites `src/core/LanguageTable.inc` and `src/core/SetupTexts.inc`. Commit both files with the
   texts. The Settings layout test lays out every page in every language from the packs built from this folder, and
   fails if they are not the ones `LanguageTable.inc` describes.
4. `scripts/package.ps1` builds the packs into `dist/AetherShell-<version>-languages/` and verifies them. Every pack
   is published with the release, next to the setup and its checksums.
