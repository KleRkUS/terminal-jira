// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adf.hpp"

#include <sstream>
#include <vector>

using nlohmann::json;

namespace adf {
namespace {

// Starts a new line, used between items of the same list.
void append_block(std::string& out, const std::string& s) {
  if (!out.empty() && out.back() != '\n') out += '\n';
  out += s;
}

// Starts a new line preceded by a blank one, which is how paragraph-level
// blocks are separated so that from_text() can recover them.
void append_paragraph(std::string& out, const std::string& s) {
  if (!out.empty()) {
    if (out.back() != '\n') out += '\n';
    if (out.size() >= 2 && out[out.size() - 2] != '\n') out += '\n';
  }
  out += s;
}

void walk(const json& node, std::string& out, int depth, int list_index) {
  if (node.is_array()) {
    for (const auto& child : node) walk(child, out, depth, list_index);
    return;
  }
  if (!node.is_object()) return;

  const std::string type = node.value("type", "");
  const json& content = node.contains("content") ? node["content"] : json::array();
  const std::string indent(static_cast<size_t>(depth > 0 ? depth * 2 : 0), ' ');

  if (type == "text") {
    out += node.value("text", "");
    return;
  }
  if (type == "hardBreak") {
    out += '\n';
    return;
  }
  if (type == "mention") {
    // attrs.text usually already carries the leading "@".
    const std::string name = node.value("attrs", json::object()).value("text", "");
    out += name.rfind('@', 0) == 0 ? name : "@" + name;
    return;
  }
  if (type == "emoji") {
    const auto attrs = node.value("attrs", json::object());
    out += attrs.value("text", attrs.value("shortName", ""));
    return;
  }
  if (type == "inlineCard") {
    out += node.value("attrs", json::object()).value("url", "");
    return;
  }
  if (type == "rule") {
    append_paragraph(out, indent + "----------");
    out += '\n';
    return;
  }
  if (type == "heading") {
    const int level = node.value("attrs", json::object()).value("level", 1);
    append_paragraph(out, std::string(static_cast<size_t>(level), '#') + ' ');
    walk(content, out, depth, 0);
    out += '\n';
    return;
  }
  if (type == "paragraph") {
    append_paragraph(out, indent);
    walk(content, out, depth, 0);
    out += '\n';
    return;
  }
  if (type == "codeBlock") {
    append_paragraph(out, indent + "```\n");
    walk(content, out, depth, 0);
    out += "\n" + indent + "```\n";
    return;
  }
  if (type == "blockquote") {
    append_paragraph(out, indent + "> ");
    walk(content, out, depth, 0);
    return;
  }
  if (type == "bulletList" || type == "orderedList") {
    int i = type == "orderedList" ? node.value("attrs", json::object()).value("order", 1) : 0;
    for (const auto& item : content) {
      walk(item, out, depth, i);
      if (i) ++i;
    }
    return;
  }
  if (type == "listItem") {
    const std::string marker = list_index > 0 ? std::to_string(list_index) + ". " : "- ";
    append_block(out, indent + marker);
    // Render the item's own paragraphs inline, nest anything deeper.
    for (const auto& child : content) {
      if (child.value("type", "") == "paragraph") {
        walk(child.contains("content") ? child["content"] : json::array(), out, 0, 0);
        out += '\n';
      } else {
        walk(child, out, depth + 1, 0);
      }
    }
    return;
  }
  if (type == "table") {
    for (const auto& row : content) {
      append_block(out, indent + "| ");
      for (const auto& cell : row.value("content", json::array())) {
        walk(cell.value("content", json::array()), out, 0, 0);
        out += " | ";
      }
      out += '\n';
    }
    return;
  }
  if (type == "mediaSingle" || type == "mediaGroup" || type == "media") {
    append_paragraph(out, indent + "[attachment]");
    out += '\n';
    return;
  }

  // doc, panel, and anything unknown: recurse so no text is lost.
  walk(content, out, depth, list_index);
}

}  // namespace

std::string to_text(const json& node) {
  if (node.is_null()) return "";
  if (node.is_string()) return node.get<std::string>();  // api/2 style wiki markup
  std::string out;
  walk(node, out, 0, 0);
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}

json from_text(const std::string& text) {
  json content = json::array();
  std::istringstream in(text);
  std::string line;
  std::vector<std::string> paragraph;

  auto flush = [&] {
    if (paragraph.empty()) return;
    json nodes = json::array();
    for (size_t i = 0; i < paragraph.size(); ++i) {
      if (i) nodes.push_back({{"type", "hardBreak"}});
      if (!paragraph[i].empty()) nodes.push_back({{"type", "text"}, {"text", paragraph[i]}});
    }
    content.push_back({{"type", "paragraph"}, {"content", nodes}});
    paragraph.clear();
  };

  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) {
      flush();
    } else {
      paragraph.push_back(line);
    }
  }
  flush();

  // Jira rejects a document with an empty paragraph list; use a bare doc.
  return {{"type", "doc"}, {"version", 1}, {"content", content}};
}

}  // namespace adf
