# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Lists the wide string literals of the given C++ sources that read as interface text.

Adjacent literals (L"a " L"b") are joined as the compiler joins them. Comments are skipped. The heuristic keeps
text with a letter and either a space or a capital followed by lowercase letters, and drops identifiers, paths,
registry keys, class names and format-only strings. Its output is a starting point for the catalog in
src/core/Translations.inc, reviewed by hand (tools/i18n/README.md).

  python tools/i18n/extract_strings.py src/settings/ui/SettingsWindow.cpp ... > strings.txt
"""
import re
import sys

ESCAPES = {'n': '\n', 'r': '\r', 't': '\t', '\\': '\\', '"': '"', "'": "'", '0': '\0'}


def literals(source):
    """Yields (line, text) for every run of adjacent wide string literals."""
    i, n, line = 0, len(source), 1
    run, run_line = None, 0
    while i < n:
        c = source[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if source.startswith('//', i):
            j = source.find('\n', i)
            i = n if j < 0 else j
            continue
        if source.startswith('/*', i):
            j = source.find('*/', i + 2)
            j = n if j < 0 else j + 2
            line += source.count('\n', i, j)
            i = j
            continue
        if source.startswith('R"', i) or source.startswith('LR"', i):
            # Raw strings (shaders, JSON samples) are never interface text.
            start = source.index('"', i) + 1
            delim = source[start:source.index('(', start)]
            end = source.index(')' + delim + '"', start) + len(delim) + 2
            line += source.count('\n', i, end)
            i = end
            continue
        if c == "'" :
            j = i + 1
            while j < n and source[j] != "'":
                j += 2 if source[j] == '\\' else 1
            i = j + 1
            continue
        if c == '"' or (c == 'L' and i + 1 < n and source[i + 1] == '"' and (i == 0 or not (source[i - 1].isalnum() or source[i - 1] == '_'))):
            wide = c == 'L'
            j = i + (2 if wide else 1)
            text = []
            while j < n and source[j] != '"':
                if source[j] == '\\':
                    nxt = source[j + 1]
                    if nxt == 'x':
                        k = j + 2
                        while k < n and source[k] in '0123456789abcdefABCDEF':
                            k += 1
                        text.append(chr(int(source[j + 2:k], 16)))
                        j = k
                        continue
                    if nxt == 'u':
                        text.append(chr(int(source[j + 2:j + 6], 16)))
                        j += 6
                        continue
                    text.append(ESCAPES.get(nxt, nxt))
                    j += 2
                    continue
                text.append(source[j])
                j += 1
            value = ''.join(text)
            if wide:
                if run is None:
                    run, run_line = value, line
                else:
                    run += value
            else:
                if run is not None:
                    yield run_line, run
                    run = None
            i = j + 1
            # Is the next token another wide literal? Then the run continues.
            k = i
            while k < n and source[k] in ' \t\r\n':
                k += 1
            if not (k + 1 < n and source[k] == 'L' and source[k + 1] == '"'):
                if run is not None:
                    yield run_line, run
                    run = None
            continue
        i += 1
    if run is not None:
        yield run_line, run


def looks_like_text(value):
    if len(value) < 2 or not re.search(r'[A-Za-z]', value):
        return False
    if re.search(r'[\\/]{1}[A-Za-z]', value) and ' ' not in value:
        return False  # paths, registry keys
    if re.fullmatch(r'[A-Za-z0-9_.:\-{}#%]+', value) and not re.fullmatch(r'&?[A-Z][a-z&]+', value):
        return False  # identifiers, class names, codes ("AetherShell.Settings", "open", "0x{:08X}")
    if value.startswith(('http://', 'https://', 'ms-settings:', 'shell:', '::{')):
        return False
    return True


def main(paths):
    seen = {}
    for path in paths:
        source = open(path, encoding='utf-8').read()
        for line, value in literals(source):
            if looks_like_text(value):
                seen.setdefault(value, []).append(f'{path}:{line}')
    for value, where in seen.items():
        print(repr(value), '\t', where[0], f'(+{len(where) - 1})' if len(where) > 1 else '')


if __name__ == '__main__':
    main(sys.argv[1:])
