// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <nlohmann/json.hpp>

// Atlassian Document Format conversion. Jira Cloud's v3 API returns rich text
// (descriptions, comments) as an ADF node tree and expects the same on write.
namespace adf {

// Flattens an ADF node tree into plain text. Accepts null or a plain string so
// it can be pointed straight at a field that may come from an older API.
std::string to_text(const nlohmann::json& node);

// Wraps plain text into a minimal ADF document: blank lines separate
// paragraphs, single newlines become hard breaks.
nlohmann::json from_text(const std::string& text);

}  // namespace adf
