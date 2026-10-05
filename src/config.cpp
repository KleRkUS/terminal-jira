// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "translations.hpp"

namespace fs = std::filesystem;

fs::path config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "terminal-jira";
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / ".config" / "terminal-jira";
}

static fs::path config_path() { return config_dir() / "config.json"; }

static void env_override(std::string& field, const char* name) {
  if (const char* v = std::getenv(name); v && *v) field = v;
}

static bool env_flag(const char* name) {
  const char* v = std::getenv(name);
  return v && (std::string(v) == "1" || std::string(v) == "true");
}

// More than one thing can be worth mentioning at startup, and there is one line
// to mention them on.
static void add_notice(std::string& notice, const std::string& message) {
  if (message.empty()) return;
  notice += (notice.empty() ? "" : "  ") + message;
}

// The config file holds a long-lived API token, so it should not be readable by
// anyone else. Tighten it rather than refusing to start, and say so.
static std::string restrict_permissions(const fs::path& path) {
  std::error_code ec;
  const auto perms = fs::status(path, ec).permissions();
  if (ec) return "";
  const auto exposed = perms & (fs::perms::group_all | fs::perms::others_all);
  if (exposed == fs::perms::none) return "";

  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, ec);
  if (ec) return translations::tr("config.notTightened", {{"path", path.string()}});
  return translations::tr("config.tightened", {{"path", path.string()}});
}

Config load_config() {
  Config cfg;
  const auto path = config_path();
  if (fs::exists(path)) {
    std::ifstream in(path);
    auto j = nlohmann::json::parse(in);
    cfg.url = j.value("url", "");
    cfg.email = j.value("email", "");
    cfg.token = j.value("token", "");
    cfg.language = j.value("language", "");
  }
  env_override(cfg.url, "JIRA_URL");
  env_override(cfg.email, "JIRA_EMAIL");
  env_override(cfg.token, "JIRA_TOKEN");
  env_override(cfg.language, "TERMINAL_JIRA_LANG");

  // Before the first message is composed, so that the errors below come out in
  // the language the user asked for.
  add_notice(cfg.notice, translations::use_language(cfg.language));
  if (fs::exists(path)) add_notice(cfg.notice, restrict_permissions(path));

  while (!cfg.url.empty() && cfg.url.back() == '/') cfg.url.pop_back();

  if (cfg.url.empty() || cfg.token.empty())
    throw std::runtime_error(translations::tr("config.missing", {{"path", path.string()}}));

  const bool https = cfg.url.rfind("https://", 0) == 0;
  const bool http = cfg.url.rfind("http://", 0) == 0;
  if (!https && !http) throw std::runtime_error(translations::tr("config.notHttps", {{"url", cfg.url}}));
  // Basic auth over plain http puts the token on the wire in clear text, so it
  // takes a deliberate opt-in (useful against a local test server).
  if (http && !env_flag("TERMINAL_JIRA_ALLOW_HTTP"))
    throw std::runtime_error(translations::tr("config.plainHttp", {{"url", cfg.url}}));
  return cfg;
}
