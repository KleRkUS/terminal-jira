// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "translations.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "strings/catalogs.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace translations {
namespace {

// Add a language here once its catalog file exists.
struct Builtin {
  std::string_view code;
  const char* json;
};
constexpr Builtin kBuiltins[] = {{"en", kStringsEn}};

constexpr std::string_view kFallback = "en";
// The key under which a node keeps its own text while still having children.
constexpr std::string_view kSelf = "_";

using Table = std::unordered_map<std::string, std::string>;

// The tree is only a convenience for whoever edits the catalog; lookups work on
// the flattened dotted paths.
void flatten(const json& node, const std::string& path, Table& out) {
  if (node.is_string()) {
    out[path] = node.get<std::string>();
    return;
  }
  if (node.is_object()) {
    for (const auto& [name, child] : node.items())
      flatten(child, name == kSelf ? path : (path.empty() ? name : path + "." + name), out);
    return;
  }
  if (node.is_array()) {
    for (size_t i = 0; i < node.size(); ++i) flatten(node[i], path + "." + std::to_string(i), out);
    return;
  }
  throw std::runtime_error("string catalog: " + path + " is not text");
}

Table parse(std::string_view text) {
  Table table;
  // Comments are allowed so the catalog can explain itself to translators.
  json tree = json::parse(text, nullptr, true, /*ignore_comments=*/true);
  flatten(tree, "", table);
  return table;
}

const char* builtin(const std::string& code) {
  for (const Builtin& b : kBuiltins)
    if (b.code == code) return b.json;
  return nullptr;
}

fs::path locale_path(const std::string& code) { return config_dir() / "locales" / (code + ".json"); }

// The active catalog. Written only by use_language(); read from both threads,
// which is why it is behind a lock rather than a bare global.
struct Active {
  std::shared_mutex mutex;
  Table table;
  std::string code;
};

Active& active() {
  static Active instance;
  static const bool loaded = [] {
    instance.table = parse(kStringsEn);
    instance.code = std::string(kFallback);
    return true;
  }();
  (void)loaded;
  return instance;
}

std::string expand(const std::string& pattern, std::initializer_list<Arg> args) {
  std::string out;
  out.reserve(pattern.size());
  for (size_t i = 0; i < pattern.size();) {
    const size_t open = pattern.find('{', i);
    if (open == std::string::npos) {
      out.append(pattern, i, std::string::npos);
      break;
    }
    const size_t close = pattern.find('}', open);
    if (close == std::string::npos) {
      out.append(pattern, i, std::string::npos);
      break;
    }
    out.append(pattern, i, open - i);

    const std::string_view name(pattern.data() + open + 1, close - open - 1);
    const auto match = std::find_if(args.begin(), args.end(), [&](const Arg& a) { return a.first == name; });
    // An unfilled placeholder stays visible, so the mismatch is obvious rather
    // than silently leaving a gap in the sentence.
    if (match == args.end())
      out.append(pattern, open, close - open + 1);
    else
      out += match->second;
    i = close + 1;
  }
  return out;
}

}  // namespace

std::string tr(std::string_view key) {
  Active& a = active();
  std::shared_lock lock(a.mutex);
  const auto it = a.table.find(std::string(key));
  if (it == a.table.end()) return "!" + std::string(key) + "!";
  return it->second;
}

std::string tr(std::string_view key, std::initializer_list<Arg> args) { return expand(tr(key), args); }

std::vector<std::string> tr_list(std::string_view key) {
  Active& a = active();
  std::shared_lock lock(a.mutex);
  std::vector<std::string> out;
  for (size_t i = 0;; ++i) {
    const auto it = a.table.find(std::string(key) + "." + std::to_string(i));
    if (it == a.table.end()) return out;
    out.push_back(it->second);
  }
}

std::string use_language(const std::string& code) {
  if (code.empty() || code == kFallback) return "";

  Table loaded;
  if (const char* compiled = builtin(code)) {
    loaded = parse(compiled);
  } else {
    const fs::path path = locale_path(code);
    if (!fs::exists(path)) {
      std::vector<std::string> codes = available_languages();
      std::string list;
      for (const auto& c : codes) list += (list.empty() ? "" : ", ") + c;
      return tr("config.unknownLanguage", {{"code", code}, {"available", list}});
    }
    try {
      std::ifstream in(path);
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      loaded = parse(text);
    } catch (const std::exception& ex) {
      return tr("config.brokenLanguage", {{"path", path.string()}, {"error", ex.what()}});
    }
  }

  // English stays underneath: a catalog that is missing a key, or is written
  // against an older version of the app, shows English there instead of "!key!".
  Active& a = active();
  std::unique_lock lock(a.mutex);
  for (auto& [key, text] : loaded) a.table[key] = std::move(text);
  a.code = code;
  return "";
}

std::vector<std::string> available_languages() {
  std::vector<std::string> out;
  for (const Builtin& b : kBuiltins) out.emplace_back(b.code);

  std::error_code ignored;
  for (const auto& entry : fs::directory_iterator(config_dir() / "locales", ignored)) {
    if (entry.path().extension() != ".json") continue;
    const std::string code = entry.path().stem().string();
    if (std::find(out.begin(), out.end(), code) == out.end()) out.push_back(code);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string language() {
  Active& a = active();
  std::shared_lock lock(a.mutex);
  return a.code;
}

}  // namespace translations
