// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jira_client.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <curl/curl.h>

#include "adf.hpp"
#include "translations.hpp"

using nlohmann::json;

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
  return size * nmemb;
}

// A request log, for when the app and Jira disagree about what is there. Off
// unless TERMINAL_JIRA_LOG names a file. Only the worker thread makes requests,
// so no lock is needed. Credentials are never written: the line is the path, the
// status, and how much came back.
void log_request(const std::string& method, const std::string& path, long status, const std::string& body) {
  static const char* target = std::getenv("TERMINAL_JIRA_LOG");
  if (!target || !*target) return;

  std::ofstream out(target, std::ios::app);
  if (!out) return;
  out << method << " " << path << " -> " << status << " (" << body.size() << " bytes)";

  // The paging fields say whether an empty view means "nothing there" or
  // "nothing understood", which is the question a log like this gets opened for.
  auto parsed = json::parse(body, nullptr, false);
  if (parsed.is_object()) {
    for (const char* field : {"issues", "values", "columns", "transitions", "comments"})
      if (parsed.contains(field) && parsed[field].is_array())
        out << " " << field << "=" << parsed[field].size();
    if (parsed.contains("total") && parsed["total"].is_number())
      out << " total=" << parsed["total"].get<long long>();
    if (parsed.contains("errorMessages")) out << " errors=" << parsed["errorMessages"].dump();
  } else if (status >= 400) {
    out << " body=" << body.substr(0, 200);
  }
  out << "\n";
}

std::string str_or(const json& j, const char* key, const std::string& fallback = "") {
  if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
  return fallback;
}

std::string nested(const json& parent, const char* key, const char* child) {
  if (!parent.contains(key) || parent[key].is_null()) return "";
  return str_or(parent[key], child);
}

// JQL string literals are double-quoted, with backslash escapes inside.
std::string jql_quote(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

const char* kListFields = "summary,status,assignee,issuetype,priority,updated,parent";
const char* kDetailFields = "summary,status,assignee,issuetype,priority,updated,parent,description,reporter,labels";

Issue parse_issue(const json& j) {
  Issue i;
  i.key = j.value("key", "");
  if (!j.contains("fields")) return i;
  const auto& f = j["fields"];
  i.summary = str_or(f, "summary");
  i.status = nested(f, "status", "name");
  i.status_id = nested(f, "status", "id");
  i.type = nested(f, "issuetype", "name");
  i.priority = nested(f, "priority", "name");
  i.assignee = nested(f, "assignee", "displayName");
  i.reporter = nested(f, "reporter", "displayName");
  i.updated = str_or(f, "updated");
  i.parent = nested(f, "parent", "key");
  if (f.contains("description")) i.description = adf::to_text(f["description"]);
  for (const auto& l : f.value("labels", json::array()))
    if (l.is_string()) i.labels.push_back(l.get<std::string>());
  return i;
}

std::vector<Issue> parse_issues(const json& j) {
  std::vector<Issue> out;
  for (const auto& it : j.value("issues", json::array())) out.push_back(parse_issue(it));
  return out;
}

}  // namespace

JiraClient::JiraClient(Config cfg) : cfg_(std::move(cfg)) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl_ = curl_easy_init();
  if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

JiraClient::~JiraClient() {
  curl_easy_cleanup(static_cast<CURL*>(curl_));
  curl_global_cleanup();
}

std::string JiraClient::escape(const std::string& s) {
  char* e = curl_easy_escape(static_cast<CURL*>(curl_), s.c_str(), static_cast<int>(s.size()));
  std::string out(e ? e : "");
  curl_free(e);
  return out;
}

json JiraClient::request(const std::string& method, const std::string& path, const json* body) {
  CURL* c = static_cast<CURL*>(curl_);
  curl_easy_reset(c);

  const std::string url = cfg_.url + path;
  std::string response;
  std::string payload = body ? body->dump() : "";

  curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Content-Type: application/json");
  // Both schemes go through curl's auth machinery rather than a hand-built
  // Authorization header: curl then knows these are credentials and, with
  // UNRESTRICTED_AUTH off, will not replay them to another host on a redirect.
  const std::string userpwd = cfg_.email + ":" + cfg_.token;
  if (cfg_.email.empty()) {
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);  // Server/DC token
    curl_easy_setopt(c, CURLOPT_XOAUTH2_BEARER, cfg_.token.c_str());
  } else {
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);  // Cloud API token
    curl_easy_setopt(c, CURLOPT_USERPWD, userpwd.c_str());
  }
  curl_easy_setopt(c, CURLOPT_UNRESTRICTED_AUTH, 0L);

  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "terminal-jira/" TERMINAL_JIRA_VERSION);
  if (body) {
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
  }

  CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);

  // The response body is reported verbatim: Jira's own error text is more
  // precise than anything this layer could paraphrase.
  if (rc != CURLE_OK) {
    log_request(method, path, 0, curl_easy_strerror(rc));
    throw JiraError(0, curl_easy_strerror(rc), method + " " + path);
  }
  log_request(method, path, status, response);
  if (status >= 400) throw JiraError(status, response, method + " " + path);

  if (response.empty()) return json::object();
  auto parsed = json::parse(response, nullptr, false);
  if (parsed.is_discarded()) throw JiraError(status, response, translations::tr("errors.notJson", {{"path", path}}));
  return parsed;
}

std::vector<Project> JiraClient::projects() {
  std::vector<Project> out;
  for (int start = 0;;) {
    auto j = request("GET", "/rest/api/3/project/search?orderBy=name&maxResults=50&expand=lead&startAt=" +
                               std::to_string(start));
    const auto values = j.value("values", json::array());
    for (const auto& p : values)
      out.push_back({str_or(p, "id"), str_or(p, "key"), str_or(p, "name"), nested(p, "lead", "displayName")});
    if (j.value("isLast", true) || values.empty()) break;
    start += static_cast<int>(values.size());
  }
  return out;
}

std::vector<Board> JiraClient::boards(const std::string& project_key) {
  std::vector<Board> out;
  for (int start = 0;;) {
    auto j = request("GET", "/rest/agile/1.0/board?maxResults=50&startAt=" + std::to_string(start) +
                               "&projectKeyOrId=" + escape(project_key));
    const auto values = j.value("values", json::array());
    for (const auto& b : values) out.push_back({b.value("id", 0), str_or(b, "name"), str_or(b, "type")});
    if (j.value("isLast", true) || values.empty()) break;
    start += static_cast<int>(values.size());
  }
  return out;
}

std::vector<BoardColumn> JiraClient::board_columns(int board_id) {
  std::vector<BoardColumn> out;
  auto j = request("GET", "/rest/agile/1.0/board/" + std::to_string(board_id) + "/configuration");
  const auto cols = j.value("columnConfig", json::object()).value("columns", json::array());
  for (const auto& c : cols) {
    BoardColumn col;
    col.name = str_or(c, "name");
    // The configuration identifies statuses by id only, so the board view maps
    // issues to columns by status id rather than name.
    for (const auto& s : c.value("statuses", json::array())) col.status_ids.push_back(str_or(s, "id"));
    out.push_back(std::move(col));
  }
  return out;
}

IssuePage JiraClient::search(const std::string& jql, int max_results, const std::string& page_token) {
  json body = {{"jql", jql},
               {"maxResults", max_results},
               {"fields", {"summary", "status", "assignee", "issuetype", "priority", "updated", "parent"}}};
  if (!page_token.empty()) body["nextPageToken"] = page_token;

  auto j = request("POST", "/rest/api/3/search/jql", &body);
  IssuePage page;
  page.issues = parse_issues(j);
  if (j.contains("nextPageToken") && j["nextPageToken"].is_string())
    page.next_token = j["nextPageToken"].get<std::string>();
  page.is_last = page.next_token.empty();
  return page;
}

IssuePage JiraClient::board_issues(int board_id, int max_results, int start_at) {
  auto j = request("GET", "/rest/agile/1.0/board/" + std::to_string(board_id) + "/issue?startAt=" +
                              std::to_string(start_at) + "&maxResults=" + std::to_string(max_results) +
                              "&fields=" + kListFields);
  IssuePage page;
  page.issues = parse_issues(j);
  page.total = j.value("total", -1);
  page.next_start = start_at + static_cast<int>(page.issues.size());
  page.is_last = page.issues.empty() || (page.total >= 0 && page.next_start >= page.total);
  return page;
}

Issue JiraClient::issue_detail(const std::string& issue_key) {
  return parse_issue(
      request("GET", "/rest/api/3/issue/" + escape(issue_key) + "?fields=" + kDetailFields));
}

std::vector<Comment> JiraClient::comments(const std::string& issue_key) {
  std::vector<Comment> out;
  auto j = request("GET", "/rest/api/3/issue/" + escape(issue_key) + "/comment?orderBy=created&maxResults=50");
  for (const auto& c : j.value("comments", json::array())) {
    Comment cm;
    cm.author = nested(c, "author", "displayName");
    cm.created = str_or(c, "created");
    if (c.contains("body")) cm.body = adf::to_text(c["body"]);
    out.push_back(std::move(cm));
  }
  return out;
}

void JiraClient::add_comment(const std::string& issue_key, const std::string& text) {
  json body = {{"body", adf::from_text(text)}};
  request("POST", "/rest/api/3/issue/" + escape(issue_key) + "/comment", &body);
}

std::vector<IssueType> JiraClient::issue_types(const std::string& project_key) {
  std::vector<IssueType> out;
  auto j = request("GET", "/rest/api/3/issue/createmeta/" + escape(project_key) + "/issuetypes");
  for (const auto& t : j.value("issueTypes", json::array())) {
    if (t.value("subtask", false)) continue;
    out.push_back({str_or(t, "id"), str_or(t, "name")});
  }
  return out;
}

std::string JiraClient::create_issue(const std::string& project_key, const std::string& issue_type_id,
                                     const std::string& summary, const std::string& description) {
  json fields = {{"project", {{"key", project_key}}},
                 {"issuetype", {{"id", issue_type_id}}},
                 {"summary", summary}};
  if (!description.empty()) fields["description"] = adf::from_text(description);
  json body = {{"fields", fields}};
  return request("POST", "/rest/api/3/issue", &body).value("key", "");
}

void JiraClient::update_summary(const std::string& issue_key, const std::string& summary) {
  json body = {{"fields", {{"summary", summary}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

void JiraClient::update_description(const std::string& issue_key, const std::string& description) {
  json body = {{"fields", {{"description", adf::from_text(description)}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

std::vector<Priority> JiraClient::priorities() {
  std::vector<Priority> out;
  auto j = request("GET", "/rest/api/3/priority");
  if (!j.is_array()) return out;
  for (const auto& p : j) out.push_back({str_or(p, "id"), str_or(p, "name")});
  return out;
}

std::vector<Issue> JiraClient::parent_candidates(const std::string& project_key,
                                                 const std::string& exclude_key) {
  // Jira will reject a parent of the wrong type for this issue, which is the
  // only authority on the matter, so offer the project and let it decide.
  std::string jql = "project = " + jql_quote(project_key);
  if (!exclude_key.empty()) jql += " AND key != " + jql_quote(exclude_key);
  return search(jql + " ORDER BY updated DESC", 100).issues;
}

void JiraClient::update_type(const std::string& issue_key, const std::string& type_id) {
  json body = {{"fields", {{"issuetype", {{"id", type_id}}}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

void JiraClient::update_priority(const std::string& issue_key, const std::string& priority_id) {
  json body = {{"fields", {{"priority", {{"id", priority_id}}}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

void JiraClient::update_reporter(const std::string& issue_key, const std::string& account_id) {
  json body = {{"fields", {{"reporter", {{"accountId", account_id}}}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

void JiraClient::update_parent(const std::string& issue_key, const std::string& parent_key) {
  // null detaches the issue from its parent; a key moves it.
  json parent = parent_key.empty() ? json(nullptr) : json({{"key", parent_key}});
  json body = {{"fields", {{"parent", parent}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

void JiraClient::update_labels(const std::string& issue_key, const std::vector<std::string>& labels) {
  json body = {{"fields", {{"labels", labels}}}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key), &body);
}

std::vector<User> JiraClient::assignable_users(const std::string& issue_key) {
  std::vector<User> out;
  auto j = request("GET", "/rest/api/3/user/assignable/search?maxResults=100&issueKey=" + escape(issue_key));
  if (!j.is_array()) return out;
  for (const auto& u : j) out.push_back({str_or(u, "accountId"), str_or(u, "displayName")});
  return out;
}

void JiraClient::assign(const std::string& issue_key, const std::string& account_id) {
  json body = {{"accountId", account_id.empty() ? json(nullptr) : json(account_id)}};
  request("PUT", "/rest/api/3/issue/" + escape(issue_key) + "/assignee", &body);
}

std::vector<Transition> JiraClient::transitions(const std::string& issue_key) {
  std::vector<Transition> out;
  auto j = request("GET", "/rest/api/3/issue/" + escape(issue_key) + "/transitions");
  for (const auto& t : j.value("transitions", json::array()))
    out.push_back({str_or(t, "id"), str_or(t, "name"), nested(t, "to", "name")});
  return out;
}

void JiraClient::transition(const std::string& issue_key, const std::string& transition_id) {
  json body = {{"transition", {{"id", transition_id}}}};
  request("POST", "/rest/api/3/issue/" + escape(issue_key) + "/transitions", &body);
}
