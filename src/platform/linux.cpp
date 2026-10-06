// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include <unistd.h>  // getpid, setenv, unsetenv

namespace platform {
namespace {

// An editor, a browser or a clipboard helper has no business seeing the token.
constexpr const char* kHiddenEnv[] = {"JIRA_TOKEN", "JIRA_EMAIL"};

class ScrubbedEnvironment {
 public:
  ScrubbedEnvironment() {
    for (const char* name : kHiddenEnv) {
      if (const char* value = std::getenv(name); value && *value) {
        saved_.emplace_back(name, value);
        ::unsetenv(name);
      }
    }
  }
  ~ScrubbedEnvironment() {
    for (const auto& [name, value] : saved_) ::setenv(name.c_str(), value.c_str(), 1);
  }
  ScrubbedEnvironment(const ScrubbedEnvironment&) = delete;
  ScrubbedEnvironment& operator=(const ScrubbedEnvironment&) = delete;

 private:
  std::vector<std::pair<std::string, std::string>> saved_;
};

// Single quotes, so a path or a url cannot be read as more than one argument.
std::string shell_quote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  return out + "'";
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string base64(const std::string& input) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < input.size(); i += 3) {
    const size_t left = input.size() - i;
    const unsigned char a = static_cast<unsigned char>(input[i]);
    const unsigned char b = left > 1 ? static_cast<unsigned char>(input[i + 1]) : 0;
    const unsigned char c = left > 2 ? static_cast<unsigned char>(input[i + 2]) : 0;
    const unsigned int triple = (a << 16) | (b << 8) | c;
    out += kAlphabet[(triple >> 18) & 0x3f];
    out += kAlphabet[(triple >> 12) & 0x3f];
    out += left > 1 ? kAlphabet[(triple >> 6) & 0x3f] : '=';
    out += left > 2 ? kAlphabet[triple & 0x3f] : '=';
  }
  return out;
}

// Feeds `text` to a helper on its standard input. False if the helper is not
// installed (the shell exits 127) or refused the text.
bool pipe_to(const std::string& command, const std::string& text) {
  ScrubbedEnvironment scrubbed;
  // Anything the helper prints would land in the middle of the drawn frame.
  FILE* pipe = popen((command + " >/dev/null 2>&1").c_str(), "w");
  if (!pipe) return false;
  const bool written = std::fwrite(text.data(), 1, text.size(), pipe) == text.size();
  return pclose(pipe) == 0 && written;
}

// Asks the terminal itself to hold the text. This is what works on the far side
// of an ssh connection, and it has no way of saying whether it was honoured.
bool hand_to_terminal(const std::string& text) {
  FILE* tty = std::fopen("/dev/tty", "w");
  if (!tty) return false;
  const std::string sequence = "\x1b]52;c;" + base64(text) + "\a";
  const bool ok = std::fwrite(sequence.data(), 1, sequence.size(), tty) == sequence.size();
  std::fclose(tty);
  return ok;
}

}  // namespace

std::filesystem::path config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "terminal-jira";
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : ".") / ".config" / "terminal-jira";
}

FileProtection protect_file(const std::filesystem::path& path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto perms = fs::status(path, ec).permissions();
  if (ec) return FileProtection::AlreadyPrivate;
  const auto exposed = perms & (fs::perms::group_all | fs::perms::others_all);
  if (exposed == fs::perms::none) return FileProtection::AlreadyPrivate;

  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, ec);
  return ec ? FileProtection::Failed : FileProtection::Tightened;
}

bool open_in_browser(const std::string& url) {
  ScrubbedEnvironment scrubbed;
  const std::string command = "xdg-open " + shell_quote(url) + " >/dev/null 2>&1 &";
  return std::system(command.c_str()) == 0;
}

bool copy_to_clipboard(const std::string& text) {
  if (text.empty()) return false;
  // Wayland, then X11. A missing helper makes the shell exit 127, which is
  // "try the next one". The terminal is last, for an ssh session that has neither.
  for (const char* command : {"wl-copy", "xclip -selection clipboard", "xsel --clipboard --input"})
    if (pipe_to(command, text)) return true;
  return hand_to_terminal(text);
}

EditedText edit_text(const std::string& initial) {
  const char* editor = std::getenv("VISUAL");
  if (!editor || !*editor) editor = std::getenv("EDITOR");
  if (!editor || !*editor) editor = "vi";

  namespace fs = std::filesystem;
  const fs::path path = fs::temp_directory_path() / ("terminal-jira-" + std::to_string(::getpid()) + ".md");
  {
    std::ofstream file(path);
    if (!file) return {EditStatus::CannotWrite, 0, path.string(), {}};
    file << initial;
  }

  int code = -1;
  {
    ScrubbedEnvironment scrubbed;
    code = std::system((std::string(editor) + " " + shell_quote(path.string())).c_str());
  }

  EditedText out;
  out.code = code;
  if (code != 0) {
    out.status = EditStatus::Failed;
  } else {
    out.text = read_file(path);
    while (!out.text.empty() && (out.text.back() == '\n' || out.text.back() == '\r')) out.text.pop_back();
    out.status = out.text == initial ? EditStatus::Unchanged : EditStatus::Ok;
  }
  std::error_code ignored;
  fs::remove(path, ignored);
  return out;
}

std::optional<std::time_t> parse_timestamp(const std::string& jira_timestamp) {
  if (jira_timestamp.size() < 19) return std::nullopt;
  std::tm tm{};
  if (!strptime(jira_timestamp.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) return std::nullopt;
  tm.tm_isdst = -1;
  const std::time_t then = std::mktime(&tm);
  if (then == -1) return std::nullopt;
  return then;
}

}  // namespace platform
