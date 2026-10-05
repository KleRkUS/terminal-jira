// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Every string the user reads comes from here. The texts themselves live in
// src/strings/en.cpp (and one file per further language), so adding a language
// means writing a catalog, not touching the windows.
//
// Keys are dotted paths into the catalog tree: "ticket.assignee" is the field
// label, "ticket.assignee.unassigned" the placeholder shown in it. A node can
// carry its own text and children at once; see src/strings/en.cpp.
namespace translations {

// Fills a {placeholder} in a catalog string.
using Arg = std::pair<std::string_view, std::string>;

// Looks up `key`. An unknown key comes back as "!key!" rather than as an empty
// string, so a typo is visible on screen and in the test output.
std::string tr(std::string_view key);

// Looks up `key` and substitutes each {name} in it. A placeholder with no
// matching argument is left as it is, for the same reason.
std::string tr(std::string_view key, std::initializer_list<Arg> args);

// The entries of a catalog list, in catalog order: tr_list("project.tabs").
std::vector<std::string> tr_list(std::string_view key);

// Selects the catalog `tr` reads from. English is loaded first and kept as the
// fallback, so a partial translation shows English for whatever it is missing.
// Returns an empty string on success, or a message to show the user: an unknown
// language is not worth refusing to start over.
//
// Call this before the UI threads start. Everything after that point is a read.
std::string use_language(const std::string& code);

// Language codes that can be passed to use_language(): the compiled-in
// catalogs, plus any locales/<code>.json next to the config file.
std::vector<std::string> available_languages();

// The code currently in use.
std::string language();

}  // namespace translations
