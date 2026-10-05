#!/usr/bin/env bash
# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Run the TUI under a pseudo-terminal and feed it timed keystrokes.
#
# The app needs a real terminal, so it runs under script(1); keystrokes are
# piped in on a timer because every action waits on a request.
#
# Usage:
#   tests/drive.sh <binary> <recording> "<seconds>:<keys>" ...
#
# Keys go through printf %b, so escapes work: \r is Enter, \x1b is Escape,
# \x03 is Ctrl-C. Example — open the first project, open its first ticket,
# then back out twice and quit:
#
#   tests/drive.sh build/terminal-jira session.raw 2:'\r' 2.5:'\r' 3:q 2:q 2:q
set -uo pipefail

if [ "$#" -lt 3 ]; then
  # The comment block between the licence header and the first command is the
  # usage text, so there is only one copy of it.
  awk 'NR>1 && /^$/ {found=1; next} found && !/^#/ {exit} found {sub(/^# ?/, ""); print}' "$0"
  exit 64
fi

binary=$1
recording=$2
shift 2

cols=${COLS:-150}
rows=${ROWS:-40}

{
  for step in "$@"; do
    sleep "${step%%:*}"
    printf '%b' "${step#*:}"
  done
  sleep 1  # let the last frame render before stdin closes
} | timeout "${TIMEOUT:-90}" script -q -c "stty rows $rows cols $cols; $binary" /dev/null \
  > "$recording" 2>&1

# script(1) exits non-zero when the app is killed by Ctrl-C, which is a normal
# way to end a session, so the recording is what matters, not the status.
[ -s "$recording" ] || { echo "drive.sh: no output captured" >&2; exit 1; }
echo "drive.sh: wrote $recording ($(wc -c < "$recording") bytes)"
