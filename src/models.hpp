// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

struct Project {
  std::string id;
  std::string key;
  std::string name;
  std::string lead;
};

struct Board {
  int id = 0;
  std::string name;
  std::string type;  // scrum / kanban / simple
};

struct BoardColumn {
  std::string name;
  std::vector<std::string> status_ids;
};

struct Issue {
  std::string key;
  std::string summary;
  std::string status;
  std::string status_id;
  std::string type;
  std::string priority;
  std::string assignee;  // display name, empty if unassigned
  std::string reporter;
  std::string updated;
  std::string parent;       // parent key, empty if none
  std::string description;  // plain text, only filled by issue_detail()
  std::vector<std::string> labels;
};

// One page of a paginated issue list. `next_token` is used by the JQL search
// API, `next_start` by the Agile board API; `is_last` covers both.
struct IssuePage {
  std::vector<Issue> issues;
  std::string next_token;
  int next_start = 0;
  bool is_last = true;
  int total = -1;  // -1 when the endpoint does not report a total
};

struct Comment {
  std::string author;
  std::string created;
  std::string body;  // plain text
};

struct User {
  std::string account_id;
  std::string display_name;
};

struct Transition {
  std::string id;
  std::string name;
  std::string to_status;
};

struct IssueType {
  std::string id;
  std::string name;
};

struct Priority {
  std::string id;
  std::string name;
};
