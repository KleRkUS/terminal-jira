// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the Atlassian Document Format conversion, which is the one
// piece of non-trivial pure logic in the app.
#include <cassert>
#include <iostream>
#include <string>

#include "adf.hpp"

namespace {

void reading_a_document_keeps_every_kind_of_content() {
  const auto doc = nlohmann::json::parse(R"({
    "type": "doc", "version": 1, "content": [
      {"type": "heading", "attrs": {"level": 2}, "content": [{"type": "text", "text": "Steps"}]},
      {"type": "paragraph", "content": [
        {"type": "text", "text": "Call "},
        {"type": "text", "text": "GET /health", "marks": [{"type": "code"}]},
        {"type": "hardBreak"},
        {"type": "text", "text": "then retry."}]},
      {"type": "bulletList", "content": [
        {"type": "listItem", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "first"}]}]},
        {"type": "listItem", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "second"}]}]}]},
      {"type": "paragraph", "content": [{"type": "mention", "attrs": {"text": "@Ada"}}]}
    ]})");

  const std::string text = adf::to_text(doc);
  assert(text.find("## Steps") != std::string::npos);
  assert(text.find("Call GET /health") != std::string::npos);
  assert(text.find("then retry.") != std::string::npos);
  assert(text.find("- first") != std::string::npos);
  assert(text.find("- second") != std::string::npos);
  // Jira already includes the "@", so it must not be doubled.
  assert(text.find("@Ada") != std::string::npos);
  assert(text.find("@@") == std::string::npos);
}

void a_round_trip_preserves_paragraphs_and_line_breaks() {
  const std::string original = "First line\nsecond line\n\nNew paragraph";
  const auto document = adf::from_text(original);
  assert(document["content"].size() == 2);  // the blank line splits paragraphs
  assert(adf::to_text(document) == original);
}

void empty_and_missing_values_are_handled() {
  assert(adf::to_text(nlohmann::json()) == "");          // null description
  assert(adf::to_text("wiki markup") == "wiki markup");  // api/2 style string
  assert(adf::from_text("")["content"].empty());
}

}  // namespace

int main() {
  reading_a_document_keeps_every_kind_of_content();
  a_round_trip_preserves_paragraphs_and_line_breaks();
  empty_and_missing_values_are_handled();
  std::cout << "adf_test: all assertions passed\n";
  return 0;
}
