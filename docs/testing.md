# Testing

You can exercise the whole app without a Jira account. Everything here runs
offline against a stub server.

```bash
cmake -S . -B build && cmake --build build -j
cd build && ctest --output-on-failure
```

`ctest` runs two tests: `adf` (unit) and `smoke` (end to end). The smoke test is
registered only if `python3` and `script` are available.

## The pieces

| File | Role |
| --- | --- |
| `tests/fake_jira.py` | stub Jira; answers every endpoint the app calls and logs what it received |
| `tests/drive.sh` | runs the binary under a pty and feeds it timed keystrokes |
| `tests/replay.py` | reconstructs the screen from a recording so you can read or assert on it |
| `tests/smoke.sh` | ties the three together into assertions |
| `tests/adf_test.cpp` | unit tests for the ADF conversion |
| `tests/translations_test.cpp` | unit tests for the string catalog lookups |

## Running the end-to-end test

```bash
tests/smoke.sh build/terminal-jira
```

It starts the stub on port 8723, drives the binary through all three windows, and
checks both halves of the contract: what was drawn, and what was sent.
Forty checks covering navigation, the board view, board column widths and
mapping, hiding columns, mutations, copying to the clipboard, the ticket field editors, dialog cancellation, the auth scheme, and the
error popups. Output
is one line per check, and the exit status is the number of failures.

Two things it does to avoid lying to you. It pins `COLS`/`ROWS` instead of
inheriting them, because a stray terminal size in the caller's shell moves
content off screen and fails assertions that have nothing to do with the change.
And it refuses to start if something is already listening on the port, because a
leftover server from an earlier session would answer instead and the results
would be quietly wrong rather than failing.

`KEEP=1` leaves the recordings and server logs behind for inspection.

## Looking at the UI yourself

The app needs a real terminal, and anything it does waits on a request, so you
cannot simply pipe keys into it. Use the two scripts:

```bash
# Start the stub
python3 tests/fake_jira.py --log-dir /tmp/jira-logs &

# Point the app at it; plain http needs the explicit opt-in
export TERMINAL_JIRA_ALLOW_HTTP=1 JIRA_URL=http://127.0.0.1:8723
export JIRA_EMAIL=you@example.com JIRA_TOKEN=anything

# Open the first project, then its first ticket, then quit
tests/drive.sh build/terminal-jira /tmp/session.raw 2:'\r' 2.5:'\r' 3:q 2:q 2:q

# Read the last frame
python3 tests/replay.py /tmp/session.raw 1
```

Each `drive.sh` argument is `seconds:keys`. Keys go through `printf %b`, so `\r`
is Enter, `\x1b` is Escape and `\x03` is Ctrl-C. Allow two or three seconds after
any key that triggers a request.

Two things to know. Send Escape presses in separate steps — two in one write look
like a single ambiguous escape sequence to the terminal parser and get swallowed.
And finish with `\x03` or enough `q` presses to exit: if the app is still running
when stdin closes, it blocks waiting for input.

To watch it interactively instead, just run the binary in your own terminal with
the same environment variables.

## Changing the board layout

To see how cards are placed when the column configuration does not line up with
the statuses the issues are actually in:

```bash
python3 tests/fake_jira.py --board renamed-ids    # column status ids match nothing
python3 tests/fake_jira.py --board extra-status   # half the issues sit off the board
python3 tests/fake_jira.py --board no-issues      # columns, but nothing on them
```

`renamed-ids` leaves every card to be matched by status name, as happens on an
instance whose statuses were renamed and recreated. `extra-status` puts some
issues in a status no column claims, so they should appear in a column labelled
"(not on the board)" rather than vanishing. `no-issues` is the state that used to
be indistinguishable from a board that failed to load; it should say in words that
the board came back empty.

## Seeing the requests

`TERMINAL_JIRA_LOG` names a file to append one line per request to: the method,
the path, the status, the size, and how many items the paging fields reported. It
is the first thing to reach for when the app and Jira disagree about what exists.

```bash
TERMINAL_JIRA_LOG=/tmp/jira.log build/terminal-jira
```

```
GET /rest/agile/1.0/board/10/configuration -> 200 (174 bytes)
GET /rest/agile/1.0/board/10/issue?startAt=0&maxResults=200&fields=… -> 200 (5985 bytes) issues=12 total=12
```

Credentials are never written to it; neither are response bodies, except the
first 200 characters of one that failed.

## Making requests fail

To see the error popups, tell the stub to reject things:

```bash
python3 tests/fake_jira.py --fail writes   # every mutation returns 403
python3 tests/fake_jira.py --fail reads    # every GET returns 500
python3 tests/fake_jira.py --fail reads,writes
```

It replies with a realistic Jira error body (`errorMessages` plus a field-keyed
`errors` object), which is what the popup displays verbatim. Trigger two failures
within four seconds to see them stack:

```bash
tests/drive.sh build/terminal-jira /tmp/fail.raw 2:'\r' 1.2:s 1.2:' ' 1:a 1.2:' ' 0.8:'\x03'
python3 tests/replay.py /tmp/fail.raw 1
```

Popups expire on a timer rather than on input, so a test for that must send no
keys at all for the duration — the smoke test waits six seconds and asserts the
popup is gone.

## Asserting on what was sent

With `--log-dir`, the stub writes two files:

- `auth.log` — one line per request with the `Authorization` header, for checking
  the scheme and that the token is not leaking.
- `writes.log` — one line per mutation with method, path and body, which is how
  the smoke test proves a transition or a created issue produced the right JSON.

```bash
grep transitions /tmp/jira-logs/writes.log
# POST /rest/api/3/issue/ENG-1/transitions {"transition":{"id":"11"}}
```

## Checking the strings

`translations_test` covers the lookup rules. For the UI side, the catalog makes mistakes
visible on purpose: a key that does not exist renders as `!the.key!` and an
unfilled placeholder stays as `{name}`, so both turn up in a recording.

```bash
grep -nE '!\w+(\.\w+)+!|\{[a-z]+\}' /tmp/nav.txt
```

To see a translation at work without building one, drop a partial catalog in the
config directory the test is using and select it:

```bash
mkdir -p "$HOME/.config/terminal-jira/locales"
echo '{"projects":{"columns":{"key":"Flag"}}}' > "$HOME/.config/terminal-jira/locales/xx.json"
TERMINAL_JIRA_LANG=xx tests/drive.sh build/terminal-jira /tmp/xx.raw 2:'\r' 2:'\x03'
```

Everything the file leaves out stays English, which is the fallback doing its
job rather than a missing translation.

## What the harness cannot tell you

The stub is permissive: it ignores auth, accepts any payload, and returns the same
issue regardless of what you wrote. So it proves the app sends the right request
and draws the right thing — not that Jira accepts it. Field shapes the stub
simplifies (custom fields, sprint objects, permission-dependent omissions) can
still differ on a real instance. Anything touching an endpoint's response shape
deserves a check against a real Jira before you trust it.
