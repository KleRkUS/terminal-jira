// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

struct Config {
  std::string url;    // e.g. https://your-org.atlassian.net
  std::string email;  // Cloud: account email. Empty => Bearer auth (Server/DC PAT)
  std::string token;  // Cloud API token or Server/DC personal access token

  std::string language;  // string catalog to use; empty or "en" for English

  // Something the user should see at startup but which is not fatal, such as
  // having had to tighten the permissions on the config file.
  std::string notice;
};

// ~/.config/terminal-jira, or $XDG_CONFIG_HOME/terminal-jira. Also where add-on language
// catalogs live, under locales/.
std::filesystem::path config_dir();

// Loads config.json from there, then applies JIRA_URL / JIRA_EMAIL /
// JIRA_TOKEN / TERMINAL_JIRA_LANG env overrides, and selects the string catalog so
// that even the errors below are translated. Throws std::runtime_error if the
// url or token is missing, or if the url would send the token over plain http.
Config load_config();
