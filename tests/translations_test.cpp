// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the string catalog: how keys resolve, and what happens when
// one is wrong. Everything the user reads goes through here, so a mistake in
// this layer is visible on every screen.
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "translations.hpp"

namespace {

void a_key_is_a_path_through_the_tree() {
  assert(translations::tr("ticket.status") == "Status");
  assert(translations::tr("projects.columns.lead") == "Lead");
}

void a_node_can_have_both_its_own_text_and_children() {
  assert(translations::tr("ticket.assignee") == "Assignee");
  assert(translations::tr("ticket.assignee.unassigned") == "unassigned");
}

void a_list_keeps_the_order_it_is_written_in() {
  const auto tabs = translations::tr_list("project.tabs");
  assert(tabs.size() == 4);
  assert(tabs[0] == "Mine");
  assert(tabs[3] == "Board");
  assert(translations::tr_list("project.table.columns").empty());  // an object is not a list
}

void placeholders_are_filled_by_name_not_by_position() {
  assert(translations::tr("ticket.breadcrumb", {{"issue", "ENG-7"}, {"key", "ENG"}}) == "Projects › ENG › ENG-7");
  assert(translations::tr("errors.status", {{"status", "404"}}) == "HTTP 404");
}

// Both of these are mistakes in the code or the catalog rather than in the
// user's input, so they are made loud instead of being papered over.
void a_mistake_is_visible_rather_than_silent() {
  assert(translations::tr("ticket.nosuchfield") == "!ticket.nosuchfield!");
  assert(translations::tr("errors.status", {{"wrongName", "404"}}) == "HTTP {status}");
}

void an_unknown_language_is_reported_and_leaves_english_in_place() {
  const std::string message = translations::use_language("tlh");
  assert(!message.empty());
  assert(message.find("tlh") != std::string::npos);
  assert(translations::language() == "en");
  assert(translations::tr("ticket.status") == "Status");
}

}  // namespace

int main() {
  // The catalog search path depends on the config directory; keep the test off
  // whatever the developer has installed.
  ::setenv("XDG_CONFIG_HOME", "/nonexistent-for-the-translations-test", 1);

  a_key_is_a_path_through_the_tree();
  a_node_can_have_both_its_own_text_and_children();
  a_list_keeps_the_order_it_is_written_in();
  placeholders_are_filled_by_name_not_by_position();
  a_mistake_is_visible_rather_than_silent();
  an_unknown_language_is_reported_and_leaves_english_in_place();
  std::cout << "translations_test: all assertions passed\n";
  return 0;
}
