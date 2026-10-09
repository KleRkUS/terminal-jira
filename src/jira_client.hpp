// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "models.hpp"

// A failed request, carrying enough to show the user exactly what Jira said.
// `status` is 0 when the request never got a response (DNS, TLS, timeout), in
// which case `body` holds the transport error.
struct JiraError : std::runtime_error {
  JiraError(long status_code, std::string response_body, std::string summary)
      : std::runtime_error(std::move(summary)),
        status(status_code),
        body(std::move(response_body)) {}

  long status;
  std::string body;
};

// Thin synchronous wrapper over the Jira Cloud REST API v3 and Agile API 1.0.
// All methods throw std::runtime_error on transport or HTTP errors. Not
// thread-safe: it owns one curl handle, so calls must be serialized (the app
// runs them all on a single worker thread).
class JiraClient {
 public:
  explicit JiraClient(Config cfg);
  ~JiraClient();
  JiraClient(const JiraClient&) = delete;
  JiraClient& operator=(const JiraClient&) = delete;

  const std::string& base_url() const { return cfg_.url; }

  std::vector<Project> projects();
  std::vector<Board> boards(const std::string& project_key);
  std::vector<BoardColumn> board_columns(int board_id);

  // Paginated listings. Pass the previous page's token / start offset to
  // continue; defaults start from the beginning.
  IssuePage search(const std::string& jql, int max_results = 50, const std::string& page_token = "");
  IssuePage board_issues(int board_id, int max_results = 100, int start_at = 0);

  Issue issue_detail(const std::string& issue_key);
  std::vector<Comment> comments(const std::string& issue_key);
  void add_comment(const std::string& issue_key, const std::string& text);

  std::vector<IssueType> issue_types(const std::string& project_key);
  std::string create_issue(const std::string& project_key, const std::string& issue_type_id,
                           const std::string& summary, const std::string& description = "");
  void update_summary(const std::string& issue_key, const std::string& summary);
  void update_description(const std::string& issue_key, const std::string& description);

  // The choices behind the ticket window's editable fields.
  std::vector<Priority> priorities();
  // Issues that could be this one's parent: anything else in the project.
  std::vector<Issue> parent_candidates(const std::string& project_key, const std::string& exclude_key);

  void update_type(const std::string& issue_key, const std::string& type_id);
  void update_priority(const std::string& issue_key, const std::string& priority_id);
  void update_reporter(const std::string& issue_key, const std::string& account_id);
  void update_parent(const std::string& issue_key, const std::string& parent_key);  // empty => detach
  void update_labels(const std::string& issue_key, const std::vector<std::string>& labels);

  std::vector<User> assignable_users(const std::string& issue_key);

  // The choices behind the list filters. Statuses are every one any issue type
  // in the project can be in, by name, once each.
  std::vector<std::string> project_statuses(const std::string& project_key);
  std::vector<User> project_assignable_users(const std::string& project_key);
  void assign(const std::string& issue_key, const std::string& account_id);  // empty => unassign

  std::vector<Transition> transitions(const std::string& issue_key);
  void transition(const std::string& issue_key, const std::string& transition_id);

 private:
  nlohmann::json request(const std::string& method, const std::string& path,
                         const nlohmann::json* body = nullptr);
  std::string escape(const std::string& s);

  Config cfg_;
  void* curl_;  // CURL*
};
