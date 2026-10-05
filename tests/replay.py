#!/usr/bin/env python3
# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Turn a recorded terminal session into readable text frames.

FTXUI redraws by moving the cursor back to the top of the previous frame and
overwriting it, so a raw recording is unreadable. This replays just enough of a
terminal (cursor up/down, carriage return, erase-to-end-of-line, colour codes
discarded) to reconstruct what was on screen, and prints the last N frames.

Usage:
    python3 tests/replay.py session.raw [frames] [--cols 150] [--rows 40]
"""
import argparse
import re

ANSI = re.compile(r"\x1b\[([0-9;?]*)([a-zA-Z])")


def frames(data, cols, rows):
    grid = [[" "] * cols for _ in range(rows)]
    captured = []
    row = col = 0
    i = 0

    while i < len(data):
        char = data[i]
        if char == "\x1b":
            match = ANSI.match(data, i)
            if not match:
                i += 1
                continue
            argument, command = match.group(1), match.group(2)
            count = int(argument) if argument.isdigit() else 1
            if command == "A":
                # Returning to the top of the screen starts a new frame.
                if row - count <= 0:
                    captured.append("\n".join("".join(r).rstrip() for r in grid))
                row = max(0, row - count)
            elif command == "B":
                row = min(rows - 1, row + count)
            elif command == "K":
                for x in range(col, cols):
                    grid[row][x] = " "
            elif command == "H":
                row = col = 0
            i = match.end()
            continue

        if char == "\r":
            col = 0
        elif char == "\n":
            row = min(rows - 1, row + 1)
            col = 0
        elif char >= " ":
            if row < rows and col < cols:
                grid[row][col] = char
            col += 1
        i += 1

    captured.append("\n".join("".join(r).rstrip() for r in grid))
    return captured


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("recording")
    parser.add_argument("frames", nargs="?", type=int, default=1)
    parser.add_argument("--cols", type=int, default=150)
    parser.add_argument("--rows", type=int, default=40)
    args = parser.parse_args()

    with open(args.recording, "rb") as handle:
        data = handle.read().decode("utf8", "replace")

    captured = frames(data, args.cols, args.rows)
    for frame in captured[-args.frames:]:
        print("=" * 70)
        print(frame.rstrip())


if __name__ == "__main__":
    main()
