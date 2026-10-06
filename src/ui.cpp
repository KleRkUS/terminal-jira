// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>

#include <ftxui/dom/node.hpp>
#include <ftxui/dom/requirement.hpp>
#include <ftxui/screen/box.hpp>

#include "platform.hpp"
#include "translations.hpp"

using namespace ftxui;

namespace ui {
namespace {

// Children get a slice of the available width each, instead of hbox()'s share
// proportional to what their content asks for. See ui::equal_columns.
class EqualColumns : public Node {
 public:
  explicit EqualColumns(Elements children) : Node(std::move(children)) {}

  void ComputeRequirement() override {
    requirement_ = Requirement();
    for (auto& child : children_) {
      child->ComputeRequirement();
      if (requirement_.selection < child->requirement().selection) {
        requirement_.selection = child->requirement().selection;
        requirement_.selected_box = child->requirement().selected_box;
      }
      requirement_.min_y = std::max(requirement_.min_y, child->requirement().min_y);
      // A child that cannot grow — a separator between two columns — keeps the
      // width it asked for; the columns themselves are promised one cell and
      // given an equal share of the rest in SetBox.
      requirement_.min_x += child->requirement().flex_grow_x == 0 ? child->requirement().min_x : 1;
    }
    requirement_.flex_grow_x = 1;
    requirement_.flex_shrink_x = 1;
  }

  void SetBox(Box box) override {
    Node::SetBox(box);

    int fixed = 0, columns = 0;
    for (auto& child : children_) {
      if (child->requirement().flex_grow_x == 0)
        fixed += child->requirement().min_x;
      else
        ++columns;
    }
    const int share = std::max(0, box.x_max - box.x_min + 1 - fixed);

    int x = box.x_min;
    int taken = 0;
    for (auto& child : children_) {
      int width = child->requirement().min_x;
      if (child->requirement().flex_grow_x != 0) {
        // The remainder goes to the leftmost columns, one cell each, so no two
        // columns ever differ by more than that.
        width = share / std::max(columns, 1) + (taken < share % std::max(columns, 1) ? 1 : 0);
        ++taken;
      }
      Box child_box = box;
      child_box.x_min = x;
      child_box.x_max = x + width - 1;
      child->SetBox(child_box);
      x = child_box.x_max + 1;
    }
  }
};

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

bool Filter::on_event(const Event& event) {
  if (!active) {
    if (event == Event::Character('/')) {
      active = true;
      query.clear();
      return true;
    }
    return false;
  }
  if (event == Event::Escape) {
    clear();
    return true;
  }
  if (event == Event::Backspace) {
    if (!query.empty()) query.pop_back();
    return true;
  }
  // Leave navigation and submission to the list itself.
  if (event == Event::Return || event == Event::ArrowUp || event == Event::ArrowDown) return false;
  if (event.is_character()) {
    query += event.character();
    return true;
  }
  return false;
}

bool matches(const std::string& haystack, const std::string& query) {
  if (query.empty()) return true;
  return lower(haystack).find(lower(query)) != std::string::npos;
}

std::string describe_failure(long status, const std::string& body) {
  std::string detail = body;
  // Bodies arrive as one long line of JSON or, from a proxy, a page of HTML.
  // Collapse the whitespace so the popup can wrap it sensibly.
  std::string collapsed;
  bool in_space = false;
  for (char c : detail) {
    const bool space = c == '\n' || c == '\r' || c == '\t' || c == ' ';
    if (space) {
      if (!in_space && !collapsed.empty()) collapsed += ' ';
    } else {
      collapsed += c;
    }
    in_space = space;
  }
  while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();

  if (status == 0) return collapsed.empty() ? translations::tr("errors.noResponse") : collapsed;
  const std::string code = std::to_string(status);
  return collapsed.empty() ? translations::tr("errors.status", {{"status", code}})
                           : translations::tr("errors.statusWithBody", {{"status", code}, {"body", collapsed}});
}

bool is_select(const Event& event) {
  return event == Event::Return || event == Event::Character(' ');
}

bool is_back(const Event& event) {
  return event == Event::Escape || event == Event::Character('q');
}

bool motion(const Event& event, int& index, int size) {
  if (size <= 0) return false;
  const int last = size - 1;
  int next = index;

  if (event == Event::Character('j') || event == Event::ArrowDown) next = index + 1;
  else if (event == Event::Character('k') || event == Event::ArrowUp) next = index - 1;
  else if (event == Event::Character('g') || event == Event::Home) next = 0;
  else if (event == Event::Character('G') || event == Event::End) next = last;
  else if (event == Event::Character('d') || event == Event::PageDown) next = index + 10;
  else if (event == Event::Character('u') || event == Event::PageUp) next = index - 10;
  else return false;

  index = std::clamp(next, 0, last);
  return true;
}

Element panel(const std::string& title, Element content, bool active) {
  auto label = text(" " + title + " ");
  label = active ? (label | bold | color(Color::Green)) : (label | dim);
  auto box = window(label, std::move(content));
  return active ? box : (box | dim);
}

Element simple_list(const std::vector<std::string>& lines, int selected, bool active) {
  Elements rows;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    auto row = text(" " + lines[i]) | flex;
    if (i == selected) row = active ? (row | inverted | focus) : (row | bold | focus);
    rows.push_back(row);
  }
  if (rows.empty()) rows.push_back(empty_hint(translations::tr("common.nothingHere")));
  return vbox(std::move(rows)) | vscroll_indicator | yframe | flex;
}

Element tab_bar(const std::vector<std::string>& names, int selected) {
  Elements cells;
  for (int i = 0; i < static_cast<int>(names.size()); ++i) {
    auto cell = text(" " + names[i] + " ");
    cells.push_back(i == selected ? (cell | bold | color(Color::Green) | underlined) : (cell | dim));
    if (i + 1 < static_cast<int>(names.size())) cells.push_back(text("·") | dim);
  }
  return hbox(std::move(cells));
}

Element equal_columns(Elements children) {
  if (children.empty()) return emptyElement();
  return std::make_shared<EqualColumns>(std::move(children));
}

Element key_value(const std::string& key, const std::string& value, int key_width) {
  return hbox({
      text(key) | dim | size(WIDTH, EQUAL, key_width),
      text(value.empty() ? translations::tr("common.noValue") : value) | flex,
  });
}

Element empty_hint(const std::string& text_value) { return text(" " + text_value) | dim; }

Color status_color(const std::string& status) {
  const std::string s = lower(status);
  if (s.find("done") != std::string::npos || s.find("closed") != std::string::npos ||
      s.find("resolved") != std::string::npos || s.find("complete") != std::string::npos)
    return Color::Green;
  if (s.find("progress") != std::string::npos || s.find("review") != std::string::npos ||
      s.find("testing") != std::string::npos)
    return Color::Yellow;
  if (s.find("block") != std::string::npos) return Color::Red;
  return Color::Blue;
}

Element avatar(const std::string& display_name) {
  if (display_name.empty()) return text("··") | dim;
  std::string initials;
  std::istringstream in(display_name);
  std::string word;
  while (in >> word && initials.size() < 2)
    if (static_cast<unsigned char>(word[0]) < 0x80) initials += static_cast<char>(std::toupper(word[0]));
  if (initials.empty()) initials = "?";
  return text(initials) | bold | color(Color::Magenta);
}

std::string relative_time(const std::string& jira_timestamp) {
  const auto when = platform::parse_timestamp(jira_timestamp);
  if (!when) return "";
  double seconds = std::difftime(std::time(nullptr), *when);
  if (seconds < 0) seconds = 0;

  const long minutes = static_cast<long>(seconds) / 60;
  if (minutes < 1) return translations::tr("time.justNow");
  if (minutes < 60) return translations::tr("time.minutes", {{"count", std::to_string(minutes)}});
  if (minutes < 60 * 24) return translations::tr("time.hours", {{"count", std::to_string(minutes / 60)}});
  if (minutes < 60 * 24 * 30) return translations::tr("time.days", {{"count", std::to_string(minutes / (60 * 24))}});
  return jira_timestamp.substr(0, 10);
}

}  // namespace ui
