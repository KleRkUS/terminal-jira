// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <exception>
#include <iostream>
#include <string_view>

#include "app.hpp"
#include "config.hpp"
#include "jira_client.hpp"

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--version") {
    std::cout << "terminal-jira " << TERMINAL_JIRA_VERSION << " (" << TERMINAL_JIRA_PLATFORM << ")\n";
    return 0;
  }
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
