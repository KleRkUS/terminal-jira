# Contributing

## What it is built from

| Technology | Why | How it arrives |
| --- | --- | --- |
| **C++20** | `std::filesystem`, designated initialisers, `std::clamp` | your compiler |
| **[FTXUI](https://github.com/ArthurSonzogni/FTXUI) 5.0** | terminal rendering and the event loop | CMake `FetchContent` |
| **[nlohmann/json](https://github.com/nlohmann/json) 3.11** | parsing and building Jira payloads | CMake `FetchContent` |
| **libcurl** | HTTPS, with auth schemes and TLS verification handled for us | system package |
| **CMake 3.20+** | build, dependency fetching, `ctest` | system package |
| **Python 3** | the stub Jira server and screen replayer used by the tests | system package |

Only libcurl, CMake and Python are installed by you. The two C++ libraries are
downloaded during `cmake` configuration, so the first configure needs network
access; after that the build is offline.

## Dependencies

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake libcurl4-openssl-dev python3

# Fedora
sudo dnf install gcc-c++ cmake libcurl-devel python3
```

The platform module is chosen at configure time, not detected from the machine:

```bash
cmake -S . -B build -DTERMINAL_JIRA_PLATFORM=linux   # the default, so it can be omitted
```

`mac` and `windows` are the other accepted values, for a user or a CI job that is
building for them. Each value compiles `src/platform/<value>.cpp` and defines
`TERMINAL_JIRA_PLATFORM` to that name on the target. `linux.cpp` and `mac.cpp`
exist; `windows` stops at configuration and names the missing file.

On Ubuntu the curl headers land in `/usr/include/x86_64-linux-gnu/curl/`, which is
already on the default include path — if `pkg-config --modversion libcurl` fails
but `/usr/lib/x86_64-linux-gnu/libcurl.so` exists, you are fine.

`script(1)` (from `util-linux`, normally present) is needed for the end-to-end
test, which has to give the app a real terminal.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Debug build with symbols:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The build is warning-clean under `-Wall -Wextra`; please keep it that way.
`compile_commands.json` is generated in `build/` for clangd — symlink it to the
project root if your editor expects it there.

`-DTERMINAL_JIRA_TESTS=OFF` skips building the tests. `-DTERMINAL_JIRA_PLATFORM`
selects the platform module; see above. The default is `linux`.

## Test

```bash
cd build && ctest --output-on-failure
```

Three tests:

- **`adf`** — unit tests for the Atlassian Document Format conversion, the one
  piece of pure logic worth testing in isolation.
- **`translations`** — unit tests for the string catalog: how a dotted key resolves, how
  placeholders are filled, and that a bad key or a missing language is reported
  rather than swallowed.
- **`smoke`** — starts a stub Jira server, drives the real binary through all
  three windows under a pseudo-terminal, and asserts on both the frames it drew
  and the requests it sent, including the error popups raised by rejected
  requests. Forty-three checks. Registered only if `python3` and `script` are
  available.

Run the end-to-end test directly for readable output:

```bash
tests/smoke.sh build/terminal-jira
```

No Jira account is needed for any of this. [docs/testing.md](docs/testing.md)
explains how to drive the UI by hand against the stub, how to read a recorded
session, and what the stub cannot prove.

## Adding a language

1. Copy `src/strings/en.cpp` to `src/strings/<code>.cpp`, rename the array
   (`kStringsDe`), and translate the values. Leave the keys and the
   `{placeholders}` alone; the placeholders may be reordered within a line.
2. Declare it in `src/strings/catalogs.hpp` and add a row to `kBuiltins` in
   `src/translations.cpp`.
3. Add the file to `CMakeLists.txt`.

A translation does not have to be complete. English is loaded first and stays
underneath, so a missing key shows English rather than `!the.key!`.

To try one without rebuilding, put the same JSON in
`~/.config/terminal-jira/locales/<code>.json` and run `TERMINAL_JIRA_LANG=<code> terminal-jira`.
That path is also how a user can translate the app themselves.

## Before opening a pull request

1. The build is clean under `-Wall -Wextra`.
2. `ctest` passes.
3. If you changed a keybinding, the window's `keys()` lists it — the footer and
   the `?` overlay are generated from it.
4. If you added a Jira call, `tests/fake_jira.py` answers it.
5. If you added any text, it is a key in `src/strings/en.cpp`, not a literal.
6. New source files are added to `CMakeLists.txt`.

Say in the pull request what you verified and what you could not. The stub server
is permissive, so anything depending on a real response shape deserves a note.

## Where things live

```
src/
  main.cpp          startup
  config.*          config file, env vars, credential hygiene
  translations.*    tr(): the string lookup
  strings/          one catalog per language; the only files with English in them
  jira_client.*     every HTTP call
  adf.*             rich text <-> plain text
  models.hpp        the data structs
  ui.*              Window base, Context interface, async, render helpers
  app.*             window stack, worker thread, dialogs, status bar, popups
  actions.*         ticket mutations shared between windows
  windows/          one file per full-screen view
tests/              stub server, pty driver, screen replayer, assertions
docs/               architecture, Jira API notes, testing
```

[docs/architecture.md](docs/architecture.md) explains the window stack, the
threading model and the handful of FTXUI behaviours that shaped the design.
[docs/jira-api.md](docs/jira-api.md) lists the endpoints and their traps. Both are
worth reading before a non-trivial change; [AGENTS.md](AGENTS.md) is the condensed
version.

## Licensing a contribution

The project is GPL-3.0-or-later, so contributions come in under the same terms.
Every source file starts with:

```cpp
// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later
```

Add that header to any new file (`#` instead of `//` for scripts and CMake).

Only add a dependency whose licence is GPL-compatible: MIT, BSD, Apache-2.0,
LGPL and the curl licence all are. If you add one, list it in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) with its licence text — the MIT
and curl licences require their notices to ship with the binary, and FTXUI is
linked statically.

One trap worth knowing: Apache-2.0 is compatible with GPLv3 but not GPLv2. Since
most distributions build libcurl against OpenSSL 3 (Apache-2.0), GPLv3 is what
makes the combination clean, and dropping to GPLv2 would not.

## Style

Follow the surrounding code. A few things that are deliberate:

- Comments state a constraint the code cannot — a protocol quirk, a threading
  rule, why the obvious simpler version fails. Not a narration of the next line.
- Names come from the domain: `board_columns`, `assignable_users`, `transitions`.
- `JiraClient` throws `JiraError`; `ui::async` catches it and raises an error
  popup. The UI does not throw.
- Every `ui::async` call is labelled with what the user was doing, in their
  words — that label is the heading of the popup when the request fails.
- No user-visible string literals outside `src/strings/`. Labels, titles, hints,
  status messages and those action labels all come from `translations::tr`, and whole
  sentences go in the catalog with `{placeholders}` so a translation can reorder
  them. Key chords, JQL and Jira's own data stay as they are.
- Jira decides what the user may do. Never gate an action on a local guess about
  permissions — ask for the transitions or the assignable users and render the
  answer.
