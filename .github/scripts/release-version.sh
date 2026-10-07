#!/usr/bin/env bash
# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Prints the version set in CMakeLists.txt. Given a pull request title such as
# "Release: 1.2.0", also fails unless the title names that same version.
#
# Usage: .github/scripts/release-version.sh ["Release: X.Y.Z"]
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
version=$(sed -nE 's/^set\(TERMINAL_JIRA_VERSION "([^"]*)"\)$/\1/p' "$root/CMakeLists.txt")

if ! [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; then
  echo "release-version: CMakeLists.txt needs a line set(TERMINAL_JIRA_VERSION \"X.Y.Z\")" \
       "(optionally X.Y.Z-suffix); found '$version'" >&2
  exit 1
fi

if [ $# -gt 0 ]; then
  named=${1#Release:}
  named=${named#"${named%%[![:space:]]*}"}
  named=${named%"${named##*[![:space:]]}"}
  named=${named#v}
  if [ "$named" != "$version" ]; then
    echo "release-version: the title '$1' names '$named', but CMakeLists.txt has $version." \
         "Bump TERMINAL_JIRA_VERSION or fix the title so they match." >&2
    exit 1
  fi
fi

echo "$version"
