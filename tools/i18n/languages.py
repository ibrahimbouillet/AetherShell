# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Aether Shell's interface languages (D70, D74, D83).

English is the source: every interface text is written in English in the code and listed in
tools/i18n/english/*.txt. Each other language is one file, tools/i18n/languages/<code>.txt:

    ## settings-pages
    en: Icon size
    tr: Taille des icônes

A variant (English (United Kingdom), Spanish (Mexico), French (Canada), Portuguese (Portugal)) lists only the texts
that differ from its base language; the rest is its base's. `\\n` is a line break, `\\t` a tab, `\\\\` a backslash.

Only English is built into the programs. Every other language is a pack, AetherShell-language-<code>.json, attached
to the version's GitHub release and downloaded when a person chooses it; its size and SHA-256 are compiled into the
programs (src/core/LanguageTable.inc), so a pack is used only if it is exactly the one this version was built with.
Setup's own texts (english/setup.txt) are built into the setup program in every language (src/core/SetupTexts.inc):
setup runs before anything can be downloaded.

  python tools/i18n/languages.py list [--from N] [--to N]   numbered English texts, for translating
  python tools/i18n/languages.py show <code> [--from N] [--to N]
  python tools/i18n/languages.py import <code> <work file> [--partial]
        work file lines: "N: translation" (N from `list`); '#' comments and blank lines are ignored.
        --partial keeps the texts already in the language file that the work file does not give.
  python tools/i18n/languages.py check [<code>...]          non-zero exit on a problem
  python tools/i18n/languages.py build [--packs DIR]         packs, LanguageTable.inc and SetupTexts.inc
  python tools/i18n/languages.py packs DIR                  only the packs of the complete languages (the build's
        tests and package.ps1 use it; nothing in the source tree is written)
  python tools/i18n/languages.py verify DIR                 non-zero exit unless DIR holds exactly the packs that
        src/core/LanguageTable.inc describes (size and SHA-256), as a release must
  python tools/i18n/languages.py status                      how complete each language is
"""
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ENGLISH = ROOT / 'tools' / 'i18n' / 'english'
LANGUAGE_DIR = ROOT / 'tools' / 'i18n' / 'languages'
TABLE_OUTPUT = ROOT / 'src' / 'core' / 'LanguageTable.inc'
SETUP_OUTPUT = ROOT / 'src' / 'core' / 'SetupTexts.inc'
DEFAULT_PACKS = ROOT / 'build' / 'language-packs'
PACK_FORMAT = 1
SETUP_AREA = 'setup'

# The order of core/Localization's Language enum, which the generated tables follow. English (United States) first,
# then by English name. code, enumerator, own name, English name, name in settings.json, right to left, base,
# formatting locale. The 38 languages Windows 11 offers for download, plus Hindi, Bengali and Urdu (D74).
LANGUAGES = [
    ('en', 'English', 'English (United States)', 'English (United States)', 'english', False, None, 'en-US'),
    ('ar', 'Arabic', 'العربية', 'Arabic', 'arabic', True, None, 'ar-SA'),
    ('bn', 'Bengali', 'বাংলা', 'Bengali', 'bengali', False, None, 'bn-BD'),
    ('bg', 'Bulgarian', 'Български', 'Bulgarian', 'bulgarian', False, None, 'bg-BG'),
    ('zh-Hans', 'Chinese', '中文（简体）', 'Chinese (Simplified)', 'chinese', False, None, 'zh-CN'),
    ('zh-Hant', 'ChineseTraditional', '中文（繁體）', 'Chinese (Traditional)', 'chinese-traditional', False, None, 'zh-TW'),
    ('hr', 'Croatian', 'Hrvatski', 'Croatian', 'croatian', False, None, 'hr-HR'),
    ('cs', 'Czech', 'Čeština', 'Czech', 'czech', False, None, 'cs-CZ'),
    ('da', 'Danish', 'Dansk', 'Danish', 'danish', False, None, 'da-DK'),
    ('nl', 'Dutch', 'Nederlands', 'Dutch', 'dutch', False, None, 'nl-NL'),
    ('en-GB', 'EnglishUnitedKingdom', 'English (United Kingdom)', 'English (United Kingdom)', 'english-uk', False, 'en', 'en-GB'),
    ('et', 'Estonian', 'Eesti', 'Estonian', 'estonian', False, None, 'et-EE'),
    ('fi', 'Finnish', 'Suomi', 'Finnish', 'finnish', False, None, 'fi-FI'),
    ('fr-CA', 'FrenchCanada', 'Français (Canada)', 'French (Canada)', 'french-canada', False, 'fr', 'fr-CA'),
    ('fr', 'French', 'Français (France)', 'French (France)', 'french', False, None, 'fr-FR'),
    ('de', 'German', 'Deutsch', 'German', 'german', False, None, 'de-DE'),
    ('el', 'Greek', 'Ελληνικά', 'Greek', 'greek', False, None, 'el-GR'),
    ('he', 'Hebrew', 'עברית', 'Hebrew', 'hebrew', True, None, 'he-IL'),
    ('hi', 'Hindi', 'हिन्दी', 'Hindi', 'hindi', False, None, 'hi-IN'),
    ('hu', 'Hungarian', 'Magyar', 'Hungarian', 'hungarian', False, None, 'hu-HU'),
    ('it', 'Italian', 'Italiano', 'Italian', 'italian', False, None, 'it-IT'),
    ('ja', 'Japanese', '日本語', 'Japanese', 'japanese', False, None, 'ja-JP'),
    ('ko', 'Korean', '한국어', 'Korean', 'korean', False, None, 'ko-KR'),
    ('lv', 'Latvian', 'Latviešu', 'Latvian', 'latvian', False, None, 'lv-LV'),
    ('lt', 'Lithuanian', 'Lietuvių', 'Lithuanian', 'lithuanian', False, None, 'lt-LT'),
    ('nb', 'Norwegian', 'Norsk bokmål', 'Norwegian (Bokmål)', 'norwegian', False, None, 'nb-NO'),
    ('pl', 'Polish', 'Polski', 'Polish', 'polish', False, None, 'pl-PL'),
    ('pt-BR', 'Portuguese', 'Português (Brasil)', 'Portuguese (Brazil)', 'portuguese', False, None, 'pt-BR'),
    ('pt-PT', 'PortuguesePortugal', 'Português (Portugal)', 'Portuguese (Portugal)', 'portuguese-portugal', False, 'pt-BR', 'pt-PT'),
    ('ro', 'Romanian', 'Română', 'Romanian', 'romanian', False, None, 'ro-RO'),
    ('ru', 'Russian', 'Русский', 'Russian', 'russian', False, None, 'ru-RU'),
    ('sr-Latn', 'SerbianLatin', 'Srpski (latinica)', 'Serbian (Latin)', 'serbian-latin', False, None, 'sr-Latn-RS'),
    ('sk', 'Slovak', 'Slovenčina', 'Slovak', 'slovak', False, None, 'sk-SK'),
    ('sl', 'Slovenian', 'Slovenščina', 'Slovenian', 'slovenian', False, None, 'sl-SI'),
    ('es', 'Spanish', 'Español (España)', 'Spanish (Spain)', 'spanish', False, None, 'es-ES'),
    ('es-MX', 'SpanishMexico', 'Español (México)', 'Spanish (Mexico)', 'spanish-mexico', False, 'es', 'es-MX'),
    ('sv', 'Swedish', 'Svenska', 'Swedish', 'swedish', False, None, 'sv-SE'),
    ('th', 'Thai', 'ไทย', 'Thai', 'thai', False, None, 'th-TH'),
    ('tr', 'Turkish', 'Türkçe', 'Turkish', 'turkish', False, None, 'tr-TR'),
    ('uk', 'Ukrainian', 'Українська', 'Ukrainian', 'ukrainian', False, None, 'uk-UA'),
    ('ur', 'Urdu', 'اردو', 'Urdu', 'urdu', True, None, 'ur-PK'),
]
CODES = [row[0] for row in LANGUAGES]
BASE = {row[0]: row[6] for row in LANGUAGES}


def pack_name(code):
    return f'AetherShell-language-{code}.json'


def unescape(value):
    out, i = [], 0
    while i < len(value):
        c = value[i]
        if c == '\\' and i + 1 < len(value):
            nxt = value[i + 1]
            out.append({'n': '\n', 't': '\t', '\\': '\\', 'r': '\r'}.get(nxt, '\\' + nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def escape(text):
    return text.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')


def read_english():
    """[(area, english, comments)] in order; english is unescaped."""
    entries, problems, seen = [], [], {}
    for path in sorted(ENGLISH.glob('*.txt')):
        area = path.stem
        comments = []
        for raw in path.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n'):
            if not raw.strip():
                continue
            if raw.startswith('#'):
                comments.append(raw[1:].strip())
                continue
            match = re.match(r'^en: ?(.*)$', raw)
            if not match:
                problems.append(f'english/{path.name}: unreadable line {raw!r}')
                continue
            text = unescape(match.group(1))
            if text in seen:
                problems.append(f'english/{path.name}: duplicate {text!r} (also in {seen[text]})')
                continue
            seen[text] = path.name
            entries.append((area, text, comments))
            comments = []
    return entries, problems


def read_language(code):
    """{english: translation} from tools/i18n/languages/<code>.txt; translations unescaped."""
    path = LANGUAGE_DIR / f'{code}.txt'
    texts, problems = {}, []
    if not path.exists():
        return texts, problems
    english = None
    for number, raw in enumerate(path.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n'), 1):
        if not raw.strip() or raw.startswith('#'):
            continue
        if raw.startswith('en: ') or raw == 'en:':
            english = unescape(raw[4:])
            continue
        if raw.startswith('tr: ') or raw == 'tr:':
            if english is None:
                problems.append(f'{code}.txt:{number}: tr: without en:')
                continue
            if english in texts:
                problems.append(f'{code}.txt:{number}: {english!r} twice')
            texts[english] = unescape(raw[4:])
            english = None
            continue
        problems.append(f'{code}.txt:{number}: unreadable line {raw!r}')
    return texts, problems


def write_language(code, texts):
    entries, _ = read_english()
    row = LANGUAGES[CODES.index(code)]
    lines = [f'# {row[3]} ({code}): {row[2]}. The interface texts of Aether Shell, translated from English.',
             '# Edit the tr: lines; keep {} placeholders, a single & access key and \\n as in the English.']
    if BASE[code]:
        lines.append(f'# A variant of {BASE[code]}: only the texts that differ from it are listed.')
    area = None
    for entry_area, english, _ in entries:
        if english not in texts:
            continue
        if entry_area != area:
            lines += ['', f'## {entry_area}']
            area = entry_area
        lines += ['', f'en: {escape(english)}', f'tr: {escape(texts[english])}']
    (LANGUAGE_DIR / f'{code}.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


def access_key(text):
    """Whether `text` marks an access key: '&' before a character, not '&&' nor a plain ' & '."""
    return re.search(r'&(?![\s&])', text.replace('&&', '')) is not None


def placeholders(text):
    return sorted(re.findall(r'\{[^{}]*\}', text.replace('{{', '').replace('}}', '')))


def check_language(code, entries=None):
    if entries is None:
        entries, _ = read_english()
    known = {english for _, english, _ in entries}
    texts, problems = read_language(code)
    for english, text in texts.items():
        if english not in known:
            problems.append(f'{code}: not an English text of the interface: {english!r}')
            continue
        if not text.strip():
            problems.append(f'{code}: empty translation for {english!r}')
            continue
        if placeholders(text) != placeholders(english):
            problems.append(f'{code}: placeholders differ for {english!r}: {text!r}')
        if access_key(text) != access_key(english):
            problems.append(f'{code}: access key (&) differs for {english!r}: {text!r}')
        if text.count('\n') != english.count('\n') and '\n' in english:
            problems.append(f'{code}: line breaks differ for {english!r}')
        if text != text.strip() and english == english.strip():
            problems.append(f'{code}: spaces around {text!r}')
        if re.match(r'^\d+: ', text):
            problems.append(f'{code}: numbered line left in {text!r}')
    if not BASE[code] and code != 'en':
        missing = [english for _, english, _ in entries if english not in texts]
        if missing:
            problems.append(f'{code}: {len(missing)} texts not translated, the first: {missing[0]!r}')
    return problems


def merged(code, entries):
    """Every translation of `code`, its base's filling the texts a variant does not list."""
    texts = {}
    if BASE[code]:
        texts.update(merged(BASE[code], entries) if BASE[code] != 'en' else {})
    own, _ = read_language(code)
    texts.update(own)
    return texts


def pack_bytes(code, entries):
    texts = merged(code, entries)
    pairs = [[english, texts[english]] for _, english, _ in entries if texts.get(english) and texts[english] != english]
    document = {'aetherLanguagePack': PACK_FORMAT, 'language': code, 'texts': pairs}
    return (json.dumps(document, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')


def literal(text):
    parts = []
    for ch in text:
        code = ord(ch)
        if ch == '\\':
            parts.append('\\\\')
        elif ch == '"':
            parts.append('\\"')
        elif ch == '\n':
            parts.append('\\n')
        elif ch == '\r':
            parts.append('\\r')
        elif ch == '\t':
            parts.append('\\t')
        elif 32 <= code < 127:
            parts.append(ch)
        elif code <= 0xFFFF:
            parts.append('\\u%04X' % code)
        else:
            parts.append('\\U%08X' % code)
    return 'L"' + ''.join(parts) + '"'


def numbered(args, entries):
    first = int(args[args.index('--from') + 1]) if '--from' in args else 1
    last = int(args[args.index('--to') + 1]) if '--to' in args else len(entries)
    return first, last


def command_list(args):
    entries, problems = read_english()
    first, last = numbered(args, entries)
    area = None
    for index, (entry_area, english, comments) in enumerate(entries, 1):
        if index < first or index > last:
            continue
        if entry_area != area:
            print(f'## {entry_area}')
            area = entry_area
        for comment in comments:
            print(f'# {comment}')
        print(f'{index}: {escape(english)}')
    return 1 if problems else 0


def command_show(args):
    code = args[0]
    entries, _ = read_english()
    texts = merged(code, entries)
    first, last = numbered(args, entries)
    for index, (_, english, _) in enumerate(entries, 1):
        if first <= index <= last:
            print(f'{index}: {escape(english)}\n   => {escape(texts.get(english, "(not translated)"))}')
    return 0


def command_import(args):
    code, work = args[0], pathlib.Path(args[1])
    if code not in CODES or code == 'en':
        print(f'unknown language {code!r}; one of {", ".join(CODES[1:])}', file=sys.stderr)
        return 1
    entries, problems = read_english()
    texts = read_language(code)[0] if '--partial' in args else {}
    for number, raw in enumerate(work.read_text(encoding='utf-8-sig').replace('\r\n', '\n').split('\n'), 1):
        if not raw.strip() or raw.startswith('#'):
            continue
        match = re.match(r'^\s*(\d+):(?: (.*))?$', raw)
        if not match:
            problems.append(f'{work.name}:{number}: not "N: translation": {raw!r}')
            continue
        index = int(match.group(1))
        if not 1 <= index <= len(entries):
            problems.append(f'{work.name}:{number}: no text number {index}')
            continue
        text = unescape(match.group(2) or '')
        if not text.strip():
            problems.append(f'{work.name}:{number}: empty translation for {index}')
            continue
        texts[entries[index - 1][1]] = text
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    LANGUAGE_DIR.mkdir(parents=True, exist_ok=True)
    write_language(code, texts)
    found = check_language(code, entries)
    for problem in found:
        print(problem, file=sys.stderr)
    print(f'{code}: {len(texts)} texts written to tools/i18n/languages/{code}.txt; {len(found)} problem(s)')
    return 1 if found else 0


def command_check(args):
    entries, problems = read_english()
    codes = args or [code for code in CODES[1:] if (LANGUAGE_DIR / f'{code}.txt').exists()]
    for code in codes:
        problems += check_language(code, entries)
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f'{len(entries)} English texts; {len(codes)} language(s) checked; {len(problems)} problem(s)')
    return 1 if problems else 0


def command_status(args):
    entries, _ = read_english()
    for code in CODES[1:]:
        own, _ = read_language(code)
        texts = merged(code, entries)
        size = len(pack_bytes(code, entries)) if texts else 0
        print(f'{code:8} {len(own):4} own, {len(texts):4}/{len(entries)} with its base, pack {size} bytes')
    return 0


def command_build(args):
    entries, problems = read_english()
    codes = CODES[1:]
    # --allow-missing (development only): a language not translated yet gets no pack; it cannot be chosen.
    allow_missing = '--allow-missing' in args
    unavailable = set()
    for code in codes:
        if not (LANGUAGE_DIR / f'{code}.txt').exists():
            if allow_missing:
                unavailable.add(code)
                continue
            problems.append(f'{code}: no tools/i18n/languages/{code}.txt')
        else:
            found = check_language(code, entries)
            if found and allow_missing:
                unavailable.add(code)
                continue
            problems += found
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    packs = pathlib.Path(args[args.index('--packs') + 1]) if '--packs' in args else DEFAULT_PACKS
    packs.mkdir(parents=True, exist_ok=True)
    header = ['// SPDX-License-Identifier: MIT', '// Copyright (c) 2026 Ibrahim Bouillet',
              '// Generated by tools/i18n/languages.py build. Do not edit by hand.',
              '// One row per language, in Language order: what Settings shows and how its pack is checked.']
    rows = []
    for row in LANGUAGES:
        code, enumerator, own, english, saved, rtl, base, locale = row
        if code == 'en' or code in unavailable or (base and base in unavailable):
            rows.append(f'{{Language::{enumerator}, "{code}", {literal(own)}, {literal(english)}, "{saved}", {str(rtl).lower()}, '
                        f'L"{locale}", "", 0, ""}},')
            continue
        data = pack_bytes(code, entries)
        (packs / pack_name(code)).write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        rows.append(f'{{Language::{enumerator}, "{code}", {literal(own)}, {literal(english)}, "{saved}", {str(rtl).lower()}, '
                    f'L"{locale}", "{pack_name(code)}", {len(data)}, "{digest}"}},')
    TABLE_OUTPUT.write_text('\n'.join(header + rows) + '\n', encoding='utf-8', newline='\n')
    # Setup's texts, in every language, built into the setup program.
    setup = [english for area, english, _ in entries if area == SETUP_AREA]
    all_texts = {code: ({} if code in unavailable else merged(code, entries)) for code in codes}
    lines = ['// SPDX-License-Identifier: MIT', '// Copyright (c) 2026 Ibrahim Bouillet',
             '// Generated by tools/i18n/languages.py build from tools/i18n/english/setup.txt. Do not edit by hand.',
             '// Setup\'s own texts in every language, in Language order (English first): setup runs before any pack.']
    for english in setup:
        columns = [literal(english)] + [literal(all_texts[code].get(english, '')) for code in codes]
        lines.append('{{' + ', '.join(columns) + '}},')
    SETUP_OUTPUT.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    built = [code for code in codes if code not in unavailable and not (BASE[code] and BASE[code] in unavailable)]
    total = sum((packs / pack_name(code)).stat().st_size for code in built)
    print(f'{len(built)} packs ({total} bytes) in {packs}; {TABLE_OUTPUT.relative_to(ROOT)} and {SETUP_OUTPUT.relative_to(ROOT)} written'
          + (f'; not available yet: {", ".join(sorted(unavailable))}' if unavailable else ''))
    return 0


def command_packs(args):
    if not args:
        print('packs: give the folder to write them in', file=sys.stderr)
        return 1
    entries, problems = read_english()
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    packs = pathlib.Path(args[0])
    packs.mkdir(parents=True, exist_ok=True)
    complete = [code for code in CODES[1:] if (LANGUAGE_DIR / f'{code}.txt').exists() and not check_language(code, entries)]
    complete = [code for code in complete if not BASE[code] or BASE[code] in complete or BASE[code] == 'en']
    for code in complete:
        (packs / pack_name(code)).write_bytes(pack_bytes(code, entries))
    print(f'{len(complete)} packs in {packs}')
    return 0


def command_verify(args):
    if not args:
        print('verify: give the folder of the packs', file=sys.stderr)
        return 1
    packs = pathlib.Path(args[0])
    row = re.compile(r'\{Language::\w+, "([^"]+)",.*, "([^"]*)", (\d+), "([0-9a-f]*)"\},$')
    problems, count = [], 0
    for line in TABLE_OUTPUT.read_text(encoding='utf-8').splitlines():
        found = row.match(line)
        if not found:
            continue
        code, name, size, digest = found.group(1), found.group(2), int(found.group(3)), found.group(4)
        if code == 'en':
            continue
        if not name:
            problems.append(f'{code}: no pack in LanguageTable.inc (not translated when it was built)')
            continue
        path = packs / name
        if not path.exists():
            problems.append(f'{code}: {name} missing')
            continue
        data = path.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            problems.append(f'{code}: {name} is not the pack LanguageTable.inc describes; run languages.py build')
            continue
        count += 1
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f'{count} packs match src/core/LanguageTable.inc')
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    command, args = sys.argv[1], sys.argv[2:]
    commands = {'list': command_list, 'show': command_show, 'import': command_import, 'check': command_check,
                'build': command_build, 'status': command_status, 'packs': command_packs, 'verify': command_verify}
    if command not in commands:
        print(__doc__)
        return 1
    return commands[command](args)


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    sys.exit(main())
