#!/usr/bin/env python3
# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks that every key passed to tr()/tr_list() exists in the English catalog.

A missing key is not a compile error: it only shows up on screen, in whichever
corner of the UI uses it. This finds them without running the app.

Keys built at runtime cannot be read from the source. The one such pattern in
use, a literal prefix + TERMINAL_JIRA_PLATFORM, is checked against every
platform module in src/platform/.

Usage: tests/check_strings.py   (exit status 1 when a key is missing)
"""

import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "src"

catalog_source = (src / "strings" / "en.cpp").read_text()
match = re.search(r'R"json\((.*?)\)json"', catalog_source, re.S)
if not match:
    sys.exit("check_strings: no R\"json(...)json\" catalog in src/strings/en.cpp")



def without_comments(text):
    # The app parses with comments allowed; Python's json does not.
    out, i, in_string = [], 0, False
    while i < len(text):
        c = text[i]
        if in_string:
            out.append(c)
            if c == "\\":
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
            out.append(c)
        elif text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                break
            continue
        elif text.startswith("/*", i):
            i = text.index("*/", i) + 2
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


keys = set()


def flatten(node, path):
    if isinstance(node, dict):
        for name, child in node.items():
            flatten(child, path if name == "_" else (f"{path}.{name}" if path else name))
    elif isinstance(node, list):
        keys.add(path)  # tr_list() reads the list by its own name
        for i, child in enumerate(node):
            flatten(child, f"{path}.{i}")
    else:
        keys.add(path)


flatten(json.loads(without_comments(match.group(1))), "")

call = re.compile(r"\btr(?:_list)?\((.*)")
key_literal = re.compile(r'"([a-z][A-Za-z0-9]*(?:\.[A-Za-z0-9_]+)+)"')
platform_prefix = re.compile(r'"([a-z][A-Za-z0-9.]*\.)"\)?\s*\+\s*TERMINAL_JIRA_PLATFORM')
platforms = sorted(p.stem for p in (src / "platform").glob("*.cpp"))

missing = []
for file in sorted(src.rglob("*.[ch]pp")):
    if file.parent.name == "strings":
        continue
    for number, line in enumerate(file.read_text().splitlines(), 1):
        found = call.search(line)
        if not found:
            continue
        args = found.group(1)
        wanted = key_literal.findall(args)
        for prefix in platform_prefix.findall(args):
            wanted += [prefix + p for p in platforms]
        for key in wanted:
            if key not in keys:
                missing.append(f"{file.relative_to(root)}:{number}: no string for '{key}'")

for problem in missing:
    print(problem)
if missing:
    sys.exit(1)
print(f"check_strings: all keys used in src/ exist ({len(keys)} in the catalog)")
