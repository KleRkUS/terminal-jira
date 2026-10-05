// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <exception>
#include <iostream>

#include "app.hpp"
#include "config.hpp"
#include "jira_client.hpp"

int main() {
  try {
    const Config config = load_config();
    JiraClient client(config);
    App app(client);
    // Shown in the UI rather than on stderr, which the alternate screen hides.
    if (!config.notice.empty()) app.set_notice(config.notice);
    app.run();
  } catch (const std::exception& ex) {
    std::cerr << "terminal-jira: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
