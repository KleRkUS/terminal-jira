// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ctime>
#include <filesystem>
#include <optional>
#include <string>

// What the rest of the program may ask the operating system for. Nothing in
// here names a command or a path layout: those live in platform/<name>.cpp.
// The build chooses the file, with -DTERMINAL_JIRA_PLATFORM=linux (the default),
// mac or windows. The host operating system is not consulted. linux.cpp and
// mac.cpp exist; windows.cpp does not, so that choice fails configuration.

namespace platform {

// Where config.json and add-on language catalogs live.
std::filesystem::path config_dir();

// A config file holds a long-lived token, so other users must not be able to
// read it. AlreadyPrivate needs nothing said; the other two are worth a notice.
enum class FileProtection { AlreadyPrivate, Tightened, Failed };
FileProtection protect_file(const std::filesystem::path& path);

// False when no launcher accepted the url.
bool open_in_browser(const std::string& url);

// False when nothing accepted the text. A terminal asked over OSC 52 may still
// ignore it, which it gives us no way to find out, so true means "accepted".
bool copy_to_clipboard(const std::string& text);

// Suspends nothing itself: the caller restores the terminal first, because an
// editor has to own the screen. Credentials are hidden from the child.
enum class EditStatus { Ok, Unchanged, Failed, CannotWrite };
struct EditedText {
  EditStatus status = EditStatus::Failed;
  int code = 0;        // the editor's wait status, when Failed
  std::string path;    // the file that could not be written, when CannotWrite
  std::string text;    // the edited body, when Ok
};
EditedText edit_text(const std::string& initial);

// A Jira timestamp such as 2026-10-05T15:30:00.000+0400. Empty when it does not
// parse; the caller decides how to display the result.
std::optional<std::time_t> parse_timestamp(const std::string& jira_timestamp);

}  // namespace platform
