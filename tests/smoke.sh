#!/usr/bin/env bash
# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

# End-to-end check against the fake Jira server: drives the real binary through
# all three windows and asserts both what was drawn and what was sent.
#
# Usage: tests/smoke.sh [path-to-binary]      (default: build/terminal-jira)
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
binary=${1:-$(dirname "$here")/build/terminal-jira}
port=${PORT:-8723}
work=$(mktemp -d)
failures=0

# Every assertion here depends on the layout, so the terminal size is pinned
# rather than inherited: a COLS/ROWS left over in the caller's shell would move
# content off screen and fail checks that have nothing to do with the change.
export COLS=150 ROWS=40

if [ ! -x "$binary" ]; then
  echo "smoke: no binary at $binary — build it first, or pass the path" >&2
  exit 1
fi

# A stray server from an earlier run would answer instead of ours, and the
# results would be quietly wrong rather than failing.
if (exec 3<>/dev/tcp/127.0.0.1/"$port") 2>/dev/null; then
  echo "smoke: something is already listening on port $port — stop it, or set PORT" >&2
  exit 1
fi

cleanup() {
  [ -n "${server_pid:-}" ] && kill "$server_pid" 2>/dev/null
  # KEEP=1 leaves the recordings and server logs behind for inspection.
  if [ -n "${KEEP:-}" ]; then
    echo "smoke: artifacts in $work"
  else
    rm -rf "$work"
  fi
}
trap cleanup EXIT

check() {
  local label=$1 pattern=$2 file=$3
  if grep -qF -- "$pattern" "$file"; then
    echo "  ok    $label"
  else
    echo "  FAIL  $label (expected to find: $pattern)"
    failures=$((failures + 1))
  fi
}

refute() {
  local label=$1 pattern=$2 file=$3
  if grep -qF -- "$pattern" "$file"; then
    echo "  FAIL  $label (did not expect: $pattern)"
    failures=$((failures + 1))
  else
    echo "  ok    $label"
  fi
}

# Replaces the running stub with one started differently, for the scenarios that
# need the server to behave another way.
start_stub() {
  if [ -n "${server_pid:-}" ]; then
    kill "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
  fi
  python3 "$here/fake_jira.py" --port "$port" --log-dir "$work" "$@" > "$work/server.out" 2>&1 &
  server_pid=$!
  for _ in $(seq 20); do
    grep -q listening "$work/server.out" && break
    sleep 0.25
  done
}

start_stub

# The stub speaks plain http, which the app refuses without this opt-in.
export TERMINAL_JIRA_ALLOW_HTTP=1
export JIRA_URL="http://127.0.0.1:$port"
export JIRA_EMAIL=tester@example.com
export JIRA_TOKEN=smoke-token
export HOME=$work  # ignore any real config file on this machine

echo "navigation: projects -> project -> ticket -> back -> back -> quit"
"$here/drive.sh" "$binary" "$work/nav.raw" \
  2:'\r' 2.5:'\r' 3:q 2:q 2:q > /dev/null
python3 "$here/replay.py" "$work/nav.raw" 99 > "$work/nav.txt"
check "projects window lists a project"   "ENG          Engineering" "$work/nav.txt"
check "project window opens"              "Projects › ENG"           "$work/nav.txt"
check "issue table is populated"          "Make widget 1 faster"     "$work/nav.txt"
check "ticket window opens"               "Projects › ENG › ENG-1"   "$work/nav.txt"
check "ticket shows its comments"         "Looks good to me."        "$work/nav.txt"

echo "board: fourth tab renders columns of cards"
"$here/drive.sh" "$binary" "$work/board.raw" 2:'\r' 2.5:4 3:'\x03' > /dev/null
python3 "$here/replay.py" "$work/board.raw" 1 > "$work/board.txt"
check "board name is shown"               "ENG Sprint Board"         "$work/board.txt"
check "columns come from the board config" "In Progress"             "$work/board.txt"
check "cards carry brief issue info"      "ENG-3"                    "$work/board.txt"

# Columns used to be as wide as the widest card in them, so one chatty column
# squeezed its neighbours. The divider row under the headings is where that shows.
echo "board layout: the columns share the width equally"
python3 - "$work/board.txt" > "$work/widths.txt" <<'EOF'
import sys

# The row of tees under the column headings spans the whole board, so the gaps
# between its tees are the column widths.
line = next(l for l in open(sys.argv[1]) if l.startswith("├") and "┬" in l)
cuts = [i for i, c in enumerate(line.rstrip()) if c in "├┬┤"]
widths = [b - a - 1 for a, b in zip(cuts, cuts[1:])]
print("column widths:", *widths)
print("equal to within one cell" if max(widths) - min(widths) <= 1 else "uneven")
EOF
check "columns share the width evenly" "equal to within one cell" "$work/widths.txt"

echo "board columns: c opens a checklist that hides a column"
"$here/drive.sh" "$binary" "$work/hide.raw" 2:'\r' 2.5:4 2:c 1:' ' 1:'\r' 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/hide.raw" 99 > "$work/hide.txt"
check "the checklist has a box per column" "[x] In Progress" "$work/hide.txt"
check "unticking one hides the column"     "1 hidden"        "$work/hide.txt"
check "and the board says what it did"     "Hiding 1 of 3 columns." "$work/hide.txt"

echo "mutations: space picks an option, writes reach the server"
: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/write.raw" 2:'\r' 2:s 2:' ' 2.5:'\x03' > /dev/null
check "transition was posted"             '/transitions {"transition":{"id":"11"}}' "$work/writes.log"

: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/create.raw" \
  2:'\r' 2:n 2:'j\r' 1.5:'Investigate flaky test' 1.5:'\r' 2.5:'\x03' > /dev/null
check "issue was created with the chosen type" '{"id":"10002"}' "$work/writes.log"
check "issue was created with the summary"     'Investigate flaky test' "$work/writes.log"

# Writes take a second each here, so the run is still going when q and j
# arrive; both must be swallowed rather than leave the window or move the rows.
echo "visual mode: v selects a run of tickets, s moves them all"
start_stub --slow-writes 1
: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/visual.raw" \
  2:'\r' 2:v 0.5:j 0.5:j 1:s 1.5:' ' 0.8:q 0.3:j 0.3:q 4:'\x03' > /dev/null
python3 "$here/replay.py" "$work/visual.raw" 99 > "$work/visual.txt"
python3 "$here/replay.py" "$work/visual.raw" 1 > "$work/visual-last.txt"
check  "the selection is counted"             "VISUAL · 3 selected"                  "$work/visual.txt"
check  "one picker for all of them"           "Move 3 tickets"                       "$work/visual.txt"
refute "it offers only statuses all share"    "Reopened"                             "$work/visual.txt"
check  "the overlay counts through them"      "Moving ENG-2 to In Progress · 1 of 3" "$work/visual.txt"
check  "each ticket is moved"                 'ENG-3/transitions {"transition":{"id":"11"}}' "$work/writes.log"
check  "and the status bar says so"           "3 tickets are now In Progress"        "$work/visual-last.txt"
check  "keys pressed meanwhile were ignored"  "Projects › ENG"                       "$work/visual-last.txt"
refute "and the selection ends with the run"  "VISUAL ·"                             "$work/visual-last.txt"

echo "visual mode: a assigns every selected ticket"
start_stub
: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/visual-assign.raw" \
  2:'\r' 2:v 0.5:j 1:a 1.5:j 0.5:' ' 2.5:'\x03' > /dev/null
python3 "$here/replay.py" "$work/visual-assign.raw" 1 > "$work/visual-assign.txt"
check "the first is assigned"  'PUT /rest/api/3/issue/ENG-1/assignee {"accountId":"a1"}' "$work/writes.log"
check "and so is the second"   'PUT /rest/api/3/issue/ENG-2/assignee {"accountId":"a1"}' "$work/writes.log"
check "the status bar says so" "2 tickets assigned to Ada Lovelace" "$work/visual-assign.txt"

# Sorting and filtering go into the JQL, so a list loaded a page at a time is
# still in the right order; the search log is what proves it.
echo "lists: t sorts and f filters, both through the JQL"
: > "$work/search.log"
"$here/drive.sh" "$binary" "$work/query.raw" \
  2:'\r' 1.5:3 1.5:t 1:jj 0.5:' ' 1.5:t 1:jj 0.5:' ' \
  1.5:f 1:' ' 1.5:jj 0.5:' ' 0.5:'\r' \
  1.5:f 1:jj 0.5:' ' 1.5:' ' 0.5:'\r' \
  1.5:f 1:jjj 0.5:' ' 1:'12, ops-3' 0.5:'\r' \
  1.5:f 1:jjjj 0.5:' ' 1:widget 0.5:'\r' 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/query.raw" 99 > "$work/query.txt"
python3 "$here/replay.py" "$work/query.raw" 1 > "$work/query-last.txt"
check "sorting asks Jira for that order"     'ORDER BY status ASC'                   "$work/search.log"
check "the same field again reverses it"     'ORDER BY status DESC'                  "$work/search.log"
check "statuses come from the project, once" 'In Progress'                           "$work/query.txt"
check "a status filter"                      'status in ("Done")'                    "$work/search.log"
check  "parents are the loaded tickets' own"  '[ ] ENG-50  Make widget 50 faster'     "$work/query.txt"
refute "not every ticket in the project"      '[ ] ENG-1  Make widget 1'              "$work/query.txt"
check  "a parent filter"                      'parent in ("ENG-50")'                  "$work/search.log"
check "IDs, with the project filled in"      'key in ("ENG-12", "OPS-3")'            "$work/search.log"
check "a name filter"                        'summary ~ "widget"'                    "$work/search.log"
check "the list says what it is showing"     'f filter · Status Done · Parent ENG-50' "$work/query-last.txt"
check "and how it is sorted"                 't sort · Status ↓'                     "$work/query-last.txt"

echo "lists: clearing the filters keeps the sort"
: > "$work/search.log"
"$here/drive.sh" "$binary" "$work/clear.raw" \
  2:'\r' 1.5:3 1.5:t 0.5:' ' 1.5:f 1:' ' 1.5:' ' 0.5:'\r' 1.5:f 1:jjjjj 0.5:' ' 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/clear.raw" 1 > "$work/clear.txt"
check "the last search has no filter left"   'project = "ENG" ORDER BY key ASC'      "$work/search.log"
check  "the sort is still shown"             't sort · ID ↑'                         "$work/clear.txt"
refute "and no filter is"                    'f filter ·'                            "$work/clear.txt"

# A card that matches no column used to be dropped, so a board full of issues
# rendered as empty columns. Both ways that can happen are checked here.
echo "board mapping: a card is never dropped for want of a column"
start_stub --board renamed-ids
"$here/drive.sh" "$binary" "$work/renamed.raw" 2:'\r' 2.5:4 3:'\x03' > /dev/null
python3 "$here/replay.py" "$work/renamed.raw" 1 > "$work/renamed.txt"
check "columns that match by name still fill" "ENG-1"      "$work/renamed.txt"
check "the board says how much it loaded"     "12 issues"  "$work/renamed.txt"

start_stub --board extra-status
"$here/drive.sh" "$binary" "$work/offboard.raw" 2:'\r' 2.5:4 3:'\x03' > /dev/null
python3 "$here/replay.py" "$work/offboard.raw" 1 > "$work/offboard.txt"
check "a status off the board gets its own column" "Backlog (not on the board)" "$work/offboard.txt"
check "and the cards in it are shown"              "ENG-2"                      "$work/offboard.txt"

start_stub --board no-issues
TERMINAL_JIRA_LOG="$work/requests.log" \
  "$here/drive.sh" "$binary" "$work/noissues.raw" 2:'\r' 2.5:4 3:'\x03' > /dev/null
python3 "$here/replay.py" "$work/noissues.raw" 1 > "$work/noissues.txt"
check "an empty board says so"        "this board returned no issues" "$work/noissues.txt"
check "and still names the column key" "c columns"                     "$work/noissues.txt"
# The log is how a board that looks wrong gets diagnosed, so it has to say how
# much came back, not just that something did.
check "the request log counts what arrived" "issues=0 total=0" "$work/requests.log"

start_stub

# The stub assigns most cards to Ada Lovelace and leaves every fourth unassigned,
# so picking the empty assignee must leave exactly those three.
echo "board assignee: f keeps one person's cards"
"$here/drive.sh" "$binary" "$work/assignee.raw" \
  2:'\r' 2.5:4 2:f 1:j 1:' ' 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/assignee.raw" 99 > "$work/assignee.txt"
python3 "$here/replay.py" "$work/assignee.raw" 1 > "$work/assignee-last.txt"
check "the heading names the key"            "f assignee"        "$work/assignee.txt"
check "the picker lists who is on the board" "Ada Lovelace  (9)" "$work/assignee.txt"
check "choosing someone names them"          "f · Unassigned"    "$work/assignee-last.txt"
check "and only their cards remain"          "showing 3 of 12"   "$work/assignee-last.txt"

echo "ticket fields: j/k picks a field, space edits it"
"$here/drive.sh" "$binary" "$work/fields.raw" 2:'\r' 2:'\r' 1.5:jj 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/fields.raw" 1 > "$work/fields.txt"
check "every editable field is listed"    "Reporter    Grace Hopper" "$work/fields.txt"
check "read-only facts stay in the heading" "Updated "                "$work/fields.txt"

# Two fields down from Status is Priority, whose choices come from an endpoint
# nothing else uses.
: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/priority.raw" 2:'\r' 2:'\r' 1.5:jj 1.5:' ' 1.5:'\r' 2:'\x03' > /dev/null
check "the field picker wrote the choice"  '{"fields":{"priority":{"id":"1"}}}' "$work/writes.log"

# Labels are a set, so that one field is a text prompt rather than a picker.
: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/labels.raw" \
  2:'\r' 2:'\r' 1.5:jjjjjj 1.5:' ' 1.5:', urgent' 1.5:'\r' 2:'\x03' > /dev/null
check "labels were sent as a list"         '{"fields":{"labels":["perf","tui","urgent"]}}' "$work/writes.log"

# Whether the text really reached a clipboard depends on the machine, so this
# checks what the app offers and what it claims to have done.
echo "copying: y offers the ticket's own text"
"$here/drive.sh" "$binary" "$work/copy.raw" 2:'\r' 2:'\r' 2:y 1:j 1:' ' 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/copy.raw" 99 > "$work/copy.txt"
check "the copy dialog names the ticket"   "Copy from ENG-1"          "$work/copy.txt"
check "it offers the identifier"           "Identifier   ENG-1"       "$work/copy.txt"
check "choosing one says what was copied"  "copied to the clipboard"  "$work/copy.txt"

# The list and the board do not carry the description, so y has to load the issue
# before it can offer the same three lines.
echo "copying from a list or a board: y loads the highlighted ticket"
"$here/drive.sh" "$binary" "$work/copy-list.raw" 2:'\r' 2:y 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/copy-list.raw" 1 > "$work/copy-list.txt"
check "the list offers the highlighted ticket" "Copy from ENG-1"       "$work/copy-list.txt"
check "and its description was loaded"         "Details for issue 1."  "$work/copy-list.txt"

"$here/drive.sh" "$binary" "$work/copy-board.raw" 2:'\r' 2.5:4 2.5:y 2:'\x03' > /dev/null
python3 "$here/replay.py" "$work/copy-board.raw" 1 > "$work/copy-board.txt"
check "the board offers the highlighted card"  "Copy from ENG-3"       "$work/copy-board.txt"

echo "cancelling: q closes a dialog without sending anything"
: > "$work/writes.log"
"$here/drive.sh" "$binary" "$work/cancel.raw" 2:'\r' 2:a 2:q 2:'\x03' > /dev/null
if [ -s "$work/writes.log" ]; then
  echo "  FAIL  cancelled dialog still sent a request"
  failures=$((failures + 1))
else
  echo "  ok    cancelled dialog sent nothing"
fi

echo "auth: the token is sent the way the configured scheme requires"
check "basic auth is used when an email is set" "Basic" "$work/auth.log"

# A second server that rejects every mutation, to exercise the error popups.
echo "errors: a rejected request raises a popup that expires on its own"
start_stub --fail writes

# Two failures in quick succession: both should be on screen, newest below. The
# keys are close together on purpose — a popup only lives four seconds, and on a
# busy machine a slower sequence lets the first one expire before the frame is
# captured, which looks like a stacking bug and is not one.
"$here/drive.sh" "$binary" "$work/fail.raw" \
  2:'\r' 1.5:s 0.8:' ' 0.6:a 0.8:' ' 0.5:'\x03' > /dev/null
python3 "$here/replay.py" "$work/fail.raw" 1 > "$work/fail.txt"
check "popup names the action"             "Move ENG-1 to In Progress" "$work/fail.txt"
check "popup shows the status and body"    "HTTP 403" "$work/fail.txt"
# The body is wrapped across lines, so match a fragment that stays on one.
check "popup includes Jira's own response" "errorMessages" "$work/fail.txt"
check "popups stack, newest last"          "Assign ENG-1 to nobody" "$work/fail.txt"

# Same failure, then six seconds of no input at all.
"$here/drive.sh" "$binary" "$work/expire.raw" 2:'\r' 1.5:s 1.5:' ' 6:'\x03' > /dev/null
python3 "$here/replay.py" "$work/expire.raw" 1 > "$work/expire.txt"
if grep -qF "Move ENG-1" "$work/expire.txt"; then
  echo "  FAIL  popup was still on screen six seconds later"
  failures=$((failures + 1))
else
  echo "  ok    popup expired without a keypress"
fi

if [ "$failures" -eq 0 ]; then
  echo "smoke: all checks passed"
else
  echo "smoke: $failures check(s) failed"
fi
exit "$failures"
