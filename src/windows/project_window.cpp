// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows/project_window.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <memory>
#include <utility>

#include "actions.hpp"
#include "translations.hpp"
#include "windows/ticket_window.hpp"

using namespace ftxui;
using translations::tr;

namespace {

constexpr int kPageSize = 50;
constexpr int kBoardPageSize = 200;

std::string jql_quote(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string jql_list(const std::vector<std::string>& values) {
  std::string out;
  for (const std::string& value : values) out += (out.empty() ? "" : ", ") + jql_quote(value);
  return "(" + out + ")";
}

// Picker order, which is also SortField order.
constexpr const char* kSortJql[] = {"key", "summary", "status", "assignee", "parent", "updated"};
constexpr const char* kSortNames[] = {"project.sort.key",      "project.sort.summary", "project.sort.status",
                                      "project.sort.assignee", "project.sort.parent",  "project.sort.updated"};

std::string join(const std::vector<std::string>& values, const std::string& glue) {
  std::string out;
  for (const std::string& value : values) out += (out.empty() ? "" : glue) + value;
  return out;
}

}  // namespace

std::string ProjectWindow::breadcrumb() const { return tr("project.breadcrumb", {{"key", project_.key}}); }

// The tab bar, the status messages and the popup labels all name the tab, so
// they all read it from the same list.
std::string ProjectWindow::tab_name(Tab tab) {
  const auto names = tab_names();
  return tab < static_cast<int>(names.size()) ? names[static_cast<size_t>(tab)] : std::string();
}

std::vector<std::string> ProjectWindow::tab_names() { return translations::tr_list("project.tabs"); }

ProjectWindow::ProjectWindow(ui::Context& ctx, Project project) : ctx_(ctx), project_(std::move(project)) {}

void ProjectWindow::on_focus() {
  // Coming back from a ticket, the list may be out of date, so refresh it
  // quietly instead of leaving a stale row on screen.
  if (tab_ == TabBoard) {
    if (!boards_loaded_)
      load_boards();
    else
      load_board_issues(false);
  } else if (!table(static_cast<Tab>(tab_)).loaded) {
    load_table(static_cast<Tab>(tab_), false);
  } else {
    load_table(static_cast<Tab>(tab_), false, false);
  }
}

// ----------------------------------------------------------------- loading

std::string ProjectWindow::jql_for(Tab tab) const {
  std::string jql = "project = " + jql_quote(project_.key);
  if (tab == TabMine) jql += " AND assignee = currentUser() AND statusCategory != Done";
  if (tab == TabOpen) jql += " AND statusCategory != Done";

  if (!query_.statuses.empty()) jql += " AND status in " + jql_list(query_.statuses);
  if (!query_.assignees.empty() || query_.unassigned) {
    std::vector<std::string> ids, either;
    for (const User& user : query_.assignees) ids.push_back(user.account_id);
    if (!ids.empty()) either.push_back("assignee in " + jql_list(ids));
    if (query_.unassigned) either.push_back("assignee is EMPTY");
    jql += " AND (" + join(either, " OR ") + ")";
  }
  if (!query_.parents.empty()) jql += " AND parent in " + jql_list(query_.parents);
  if (!query_.keys.empty()) jql += " AND key in " + jql_list(query_.keys);
  if (!query_.name.empty()) jql += " AND summary ~ " + jql_quote(query_.name);

  // A single sort field on purpose: Jira has ignored the direction of any field
  // after `parent` in an ORDER BY.
  return jql + " ORDER BY " + kSortJql[static_cast<int>(query_.sort)] + (query_.descending ? " DESC" : " ASC");
}

void ProjectWindow::load_table(Tab tab, bool append, bool announce) {
  Table& t = table(tab);
  const int version = query_version_;
  if (t.loading && t.query_version == version) return;
  if (append && (t.is_last || t.query_version != version)) return;

  t.loading = true;
  t.loaded = true;
  t.query_version = version;
  const std::string jql = jql_for(tab);
  const std::string token = append ? t.next_token : std::string();
  if (announce)
    ctx_.set_status(append ? tr("project.status.loadingMore")
                           : tr("project.status.loadingTab", {{"tab", tab_name(tab)}}));

  ui::async(
      ctx_, life, tr("project.action.loadTab", {{"tab", tab_name(tab)}, {"project", project_.key}}),
      [this, jql, token] { return ctx_.jira().search(jql, kPageSize, token); },
      [this, tab, append, announce, version](IssuePage page) {
        if (version != query_version_) return;  // asked for under a query since replaced
        Table& t = table(tab);
        t.loading = false;
        if (append)
          t.issues.insert(t.issues.end(), page.issues.begin(), page.issues.end());
        else
          t.issues = std::move(page.issues);
        t.next_token = page.next_token;
        t.is_last = page.is_last;
        t.selected = std::clamp(t.selected, 0, std::max(0, static_cast<int>(t.issues.size()) - 1));
        if (announce)
          ctx_.set_status(tr(t.is_last ? "project.status.tabLoaded" : "project.status.tabLoadedPartial",
                             {{"project", project_.key},
                              {"tab", tab_name(tab)},
                              {"count", std::to_string(t.issues.size())}}));
      },
      [this, tab, version] {
        if (version == query_version_) table(tab).loading = false;
      });
}

void ProjectWindow::load_boards() {
  boards_loaded_ = true;
  ctx_.set_status(tr("project.status.loadingBoards"));
  ui::async(
      ctx_, life, tr("project.action.loadBoards", {{"project", project_.key}}),
      [this] { return ctx_.jira().boards(project_.key); },
      [this](std::vector<Board> boards) {
        boards_ = std::move(boards);
        if (boards_.empty()) return ctx_.set_status(tr("project.status.noBoards"), false);
        if (board_ < 0) {
          board_ = 0;
          load_board_issues();
        }
      });
}

void ProjectWindow::load_board_issues(bool announce) {
  if (board_ < 0 || board_ >= static_cast<int>(boards_.size())) return;
  const int board_id = boards_[static_cast<size_t>(board_)].id;
  const std::string board_name = boards_[static_cast<size_t>(board_)].name;
  if (announce) ctx_.set_status(tr("project.status.loadingBoard", {{"name", board_name}}));

  // The column layout and the issues are two requests, and they are two jobs on
  // purpose: the configuration needs wider permissions than the issues do, and
  // one failing must not throw away the other's answer. A card that arrives
  // without a column still gets one of its own — see board_columns().
  ui::async(
      ctx_, life, tr("project.action.loadBoardColumns", {{"name", board_name}}),
      [this, board_id] { return ctx_.jira().board_columns(board_id); },
      [this](std::vector<BoardColumn> columns) {
        config_ = std::move(columns);
        card_sel_.resize(std::max<size_t>(config_.size(), 1), 0);
      });

  ui::async(
      ctx_, life, tr("project.action.loadBoard", {{"name", board_name}}),
      [this, board_id] { return ctx_.jira().board_issues(board_id, kBoardPageSize, 0); },
      [this, announce, board_name](IssuePage page) {
        board_issues_ = std::move(page.issues);
        board_total_ = page.total;
        if (!announce) return;  // a quiet refresh leaves the cursor alone

        board_col_ = 0;
        std::fill(card_sel_.begin(), card_sel_.end(), 0);
        const bool partial = board_total_ > static_cast<int>(board_issues_.size());
        ctx_.set_status(tr(partial ? "project.status.boardLoadedPartial" : "project.status.boardLoaded",
                           {{"name", board_name},
                            {"count", std::to_string(board_issues_.size())},
                            {"total", std::to_string(board_total_)}}));
      });
}

void ProjectWindow::choose_board() {
  if (boards_.empty()) return ctx_.set_status(tr("project.status.noBoards"), true);
  std::vector<std::string> names;
  for (const auto& b : boards_)
    names.push_back(tr("project.board.pick.option", {{"name", b.name}, {"type", b.type}}));
  ctx_.choose(tr("project.board.pick", {{"key", project_.key}}), names, [this](int pick) {
    board_ = pick;
    load_board_issues();
  });
}

void ProjectWindow::choose_columns() {
  if (board_ < 0 || board_ >= static_cast<int>(boards_.size())) return;
  const auto columns = board_columns();
  if (columns.empty()) return ctx_.set_status(tr("project.board.columns.none"), true);
  const Board& board = boards_[static_cast<size_t>(board_)];

  // Only the names are carried into the callback: the columns themselves point
  // into board_issues_, which a refresh may have replaced by the time it runs.
  std::vector<std::string> names, labels;
  std::vector<bool> shown;
  for (const Column& column : columns) {
    names.push_back(column.name);
    labels.push_back(tr("project.board.columns.option",
                        {{"name", column.name}, {"count", std::to_string(column.cards.size())}}));
    shown.push_back(!column.hidden);
  }

  ctx_.check_list(tr("project.board.columns", {{"name", board.name}}), labels, shown,
                  [this, names](const std::vector<bool>& picked) {
                    // Hiding everything leaves a blank board that looks broken,
                    // which is the one answer not worth honouring.
                    if (std::find(picked.begin(), picked.end(), true) == picked.end())
                      return ctx_.set_status(tr("project.board.columns.allHidden"), true);

                    hidden_columns_.clear();
                    for (size_t i = 0; i < names.size() && i < picked.size(); ++i)
                      if (!picked[i]) hidden_columns_.push_back(names[i]);

                    board_col_ = 0;
                    const size_t hidden = hidden_columns_.size();
                    ctx_.set_status(hidden == 0 ? tr("project.board.columns.shown")
                                               : tr("project.board.columns.hiding",
                                                    {{"count", std::to_string(hidden)},
                                                     {"total", std::to_string(names.size())}}));
                  });
}

void ProjectWindow::choose_board_assignee() {
  if (board_ < 0 || board_ >= static_cast<int>(boards_.size())) return;
  const std::string board_name = boards_[static_cast<size_t>(board_)].name;

  // The names come from the cards on the board rather than from Jira: they are
  // the only ones that can change what is on screen, and they need no request.
  std::vector<std::string> names;
  for (const Issue& issue : board_issues_)
    if (std::find(names.begin(), names.end(), issue.assignee) == names.end()) names.push_back(issue.assignee);
  if (names.empty()) return ctx_.set_status(tr("project.board.assignee.none"), true);
  std::sort(names.begin(), names.end());

  std::vector<std::string> options{tr("project.board.assignee.everyone")};
  for (const std::string& name : names) {
    const int count = static_cast<int>(std::count_if(board_issues_.begin(), board_issues_.end(),
                                                    [&](const Issue& i) { return i.assignee == name; }));
    options.push_back(tr("project.board.assignee.option",
                         {{"name", name.empty() ? tr("project.board.assignee.unassigned") : name},
                          {"count", std::to_string(count)}}));
  }

  ctx_.choose(tr("project.board.assignee", {{"name", board_name}}), options,
              [this, names, board_name](int pick) {
                if (pick == 0) {
                  board_assignee_.reset();
                  ctx_.set_status(tr("project.board.assignee.cleared", {{"board", board_name}}));
                } else {
                  board_assignee_ = names[static_cast<size_t>(pick - 1)];
                  const std::string shown =
                      board_assignee_->empty() ? tr("project.board.assignee.unassigned") : *board_assignee_;
                  ctx_.set_status(tr("project.board.assignee.set", {{"name", shown}, {"board", board_name}}));
                }
                board_col_ = 0;
                std::fill(card_sel_.begin(), card_sel_.end(), 0);
              });
}

// ------------------------------------------------------ sorting and filtering

void ProjectWindow::apply_query() {
  ++query_version_;
  visual_ = false;
  for (int tab = 0; tab < TabBoard; ++tab) {
    Table& t = table(static_cast<Tab>(tab));
    t.loaded = false;
    t.loading = false;
    t.selected = 0;
  }
  if (tab_ != TabBoard) load_table(static_cast<Tab>(tab_), false);
}

std::string ProjectWindow::describe_sort() const {
  return tr("project.sort.current", {{"field", tr(kSortNames[static_cast<int>(query_.sort)])},
                                     {"direction", tr(query_.descending ? "project.sort.desc" : "project.sort.asc")}});
}

std::string ProjectWindow::describe_filters() const {
  std::vector<std::string> parts;
  if (!query_.statuses.empty())
    parts.push_back(tr("project.filter.is", {{"field", tr("project.filter.status")}, {"value", join(query_.statuses, ", ")}}));
  if (!query_.assignees.empty() || query_.unassigned) {
    std::vector<std::string> names;
    if (query_.unassigned) names.push_back(tr("project.filter.unassigned"));
    for (const User& user : query_.assignees) names.push_back(user.display_name);
    parts.push_back(tr("project.filter.is", {{"field", tr("project.filter.assignee")}, {"value", join(names, ", ")}}));
  }
  if (!query_.parents.empty())
    parts.push_back(tr("project.filter.is", {{"field", tr("project.filter.parent")}, {"value", join(query_.parents, ", ")}}));
  if (!query_.keys.empty())
    parts.push_back(tr("project.filter.is", {{"field", tr("project.filter.key")}, {"value", join(query_.keys, ", ")}}));
  if (!query_.name.empty())
    parts.push_back(tr("project.filter.contains", {{"field", tr("project.filter.summary")}, {"value", query_.name}}));
  return join(parts, " · ");
}

void ProjectWindow::choose_sort() {
  std::vector<std::string> options;
  for (int i = 0; i < static_cast<int>(std::size(kSortNames)); ++i) {
    const bool current = i == static_cast<int>(query_.sort);
    options.push_back(current ? tr("project.sort.optionCurrent",
                                   {{"field", tr(kSortNames[i])},
                                    {"direction", tr(query_.descending ? "project.sort.desc" : "project.sort.asc")}})
                              : tr(kSortNames[i]));
  }
  ctx_.choose(tr("project.sort.title"), options, [this](int pick) {
    const auto field = static_cast<SortField>(pick);
    // The same field again reverses it. A new field starts the way it is most
    // often read: newest first for dates, A to Z for everything else.
    if (field == query_.sort)
      query_.descending = !query_.descending;
    else
      query_.descending = field == SortField::Updated;
    query_.sort = field;
    apply_query();
  });
}

void ProjectWindow::choose_filter() {
  const std::string any = tr("project.filter.any");
  std::vector<std::string> statuses = query_.statuses, names, parents = query_.parents, keys = query_.keys;
  if (query_.unassigned) names.push_back(tr("project.filter.unassigned"));
  for (const User& user : query_.assignees) names.push_back(user.display_name);
  auto option = [&](const char* field, const std::string& value) {
    return tr("project.filter.option", {{"field", tr(field)}, {"value", value.empty() ? any : value}});
  };
  std::vector<std::string> options{
      option("project.filter.status", join(statuses, ", ")),
      option("project.filter.assignee", join(names, ", ")),
      option("project.filter.parent", join(parents, ", ")),
      option("project.filter.key", join(keys, ", ")),
      option("project.filter.summary", query_.name),
  };
  if (query_.filtered()) options.push_back(tr("project.filter.clear"));

  ctx_.choose(tr("project.filter.title"), options, [this](int pick) {
    switch (pick) {
      case 0: return filter_statuses();
      case 1: return filter_assignees();
      case 2: return filter_parents();
      case 3: return filter_keys();
      case 4: return filter_name();
      default: {
        ListQuery cleared;
        cleared.sort = query_.sort;
        cleared.descending = query_.descending;
        query_ = std::move(cleared);
        return apply_query();
      }
    }
  });
}

void ProjectWindow::filter_statuses() {
  ctx_.set_status(tr("project.filter.loadingStatuses"));
  ui::async(
      ctx_, life, tr("project.action.loadStatuses", {{"project", project_.key}}),
      [this] { return ctx_.jira().project_statuses(project_.key); },
      [this](std::vector<std::string> statuses) {
        if (statuses.empty()) return ctx_.set_status(tr("project.filter.noChoices"), true);
        std::vector<bool> checked;
        for (const std::string& s : statuses)
          checked.push_back(std::find(query_.statuses.begin(), query_.statuses.end(), s) != query_.statuses.end());
        ctx_.set_status("");
        ctx_.check_list(tr("project.filter.pick", {{"field", tr("project.filter.status")}}), statuses, checked,
                        [this, statuses](const std::vector<bool>& picked) {
                          query_.statuses.clear();
                          for (size_t i = 0; i < statuses.size() && i < picked.size(); ++i)
                            if (picked[i]) query_.statuses.push_back(statuses[i]);
                          apply_query();
                        });
      });
}

void ProjectWindow::filter_assignees() {
  ctx_.set_status(tr("project.filter.loadingUsers"));
  ui::async(
      ctx_, life, tr("project.action.loadUsers", {{"project", project_.key}}),
      [this] { return ctx_.jira().project_assignable_users(project_.key); },
      [this](std::vector<User> users) {
        // Unassigned is a state, not a person, so it leads the list.
        std::vector<std::string> options{tr("project.filter.unassigned")};
        std::vector<bool> checked{query_.unassigned};
        for (const User& user : users) {
          options.push_back(user.display_name);
          checked.push_back(std::any_of(query_.assignees.begin(), query_.assignees.end(),
                                        [&](const User& u) { return u.account_id == user.account_id; }));
        }
        ctx_.set_status("");
        ctx_.check_list(tr("project.filter.pick", {{"field", tr("project.filter.assignee")}}), options, checked,
                        [this, users](const std::vector<bool>& picked) {
                          query_.unassigned = !picked.empty() && picked[0];
                          query_.assignees.clear();
                          for (size_t i = 0; i < users.size() && i + 1 < picked.size(); ++i)
                            if (picked[i + 1]) query_.assignees.push_back(users[i]);
                          apply_query();
                        });
      });
}

void ProjectWindow::filter_parents() {
  // The parents of the tickets already loaded on any list tab, rather than every
  // ticket in the project: most tickets are nobody's parent. Parents already
  // ticked stay listed, or filtering by one would hide the rest for good.
  std::vector<std::string> keys = query_.parents;
  for (int tab = 0; tab < TabBoard; ++tab)
    for (const Issue& issue : table(static_cast<Tab>(tab)).issues)
      if (!issue.parent.empty() && std::find(keys.begin(), keys.end(), issue.parent) == keys.end())
        keys.push_back(issue.parent);
  if (keys.empty()) return ctx_.set_status(tr("project.filter.noParents"), true);

  // Only for the names. A parent that was deleted or moved makes Jira refuse
  // the whole `key in` query, and the keys alone are still a usable list, so a
  // failure here is not worth an error popup.
  constexpr size_t kMaxNamed = 100;
  std::vector<std::string> named(keys.begin(), keys.begin() + static_cast<long>(std::min(keys.size(), kMaxNamed)));
  ctx_.set_status(tr("project.filter.loadingParents"));
  ui::async(
      ctx_, life, tr("project.action.loadParents", {{"project", project_.key}}),
      [this, named] {
        try {
          return ctx_.jira().search("key in " + jql_list(named), static_cast<int>(named.size())).issues;
        } catch (const JiraError&) {
          return std::vector<Issue>{};
        }
      },
      [this, keys](std::vector<Issue> parents) {
        std::vector<std::string> options;
        std::vector<bool> checked;
        for (const std::string& key : keys) {
          const auto found = std::find_if(parents.begin(), parents.end(), [&](const Issue& p) { return p.key == key; });
          options.push_back(found == parents.end()
                                ? key
                                : tr("project.filter.parentOption", {{"key", key}, {"summary", found->summary}}));
          checked.push_back(std::find(query_.parents.begin(), query_.parents.end(), key) != query_.parents.end());
        }
        ctx_.set_status("");
        ctx_.check_list(tr("project.filter.pick", {{"field", tr("project.filter.parent")}}), options, checked,
                        [this, keys](const std::vector<bool>& picked) {
                          query_.parents.clear();
                          for (size_t i = 0; i < keys.size() && i < picked.size(); ++i)
                            if (picked[i]) query_.parents.push_back(keys[i]);
                          apply_query();
                        });
      });
}

void ProjectWindow::filter_keys() {
  ctx_.prompt(tr("project.filter.askKeys", {{"project", project_.key}}), join(query_.keys, ", "),
              [this](const std::string& typed) {
                // "12" is short for this project's ENG-12. Anything that is not a
                // key is refused here, where it can be named, rather than by Jira
                // as a JQL syntax error.
                std::vector<std::string> keys;
                std::string token;
                auto take = [&]() -> bool {
                  if (token.empty()) return true;
                  std::string key = token;
                  token.clear();
                  for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                  if (std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isdigit(c); }))
                    key = project_.key + "-" + key;
                  const auto dash = key.rfind('-');
                  const bool valid =
                      dash != std::string::npos && dash > 0 && dash + 1 < key.size() &&
                      std::isalpha(static_cast<unsigned char>(key[0])) &&
                      std::all_of(key.begin() + static_cast<long>(dash) + 1, key.end(),
                                  [](unsigned char c) { return std::isdigit(c); }) &&
                      std::all_of(key.begin(), key.begin() + static_cast<long>(dash),
                                  [](unsigned char c) { return std::isalnum(c) || c == '_'; });
                  if (!valid) {
                    ctx_.set_status(tr("project.filter.badKey", {{"key", key}}), true);
                    return false;
                  }
                  if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
                  return true;
                };
                for (char c : typed) {
                  if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
                    if (!take()) return;
                  } else {
                    token += c;
                  }
                }
                if (!take()) return;
                query_.keys = std::move(keys);
                apply_query();
              });
}

void ProjectWindow::filter_name() {
  ctx_.prompt(tr("project.filter.askName"), query_.name, [this](const std::string& typed) {
    query_.name = typed;
    apply_query();
  });
}

void ProjectWindow::reload(bool announce) {
  if (tab_ == TabBoard)
    load_board_issues(announce);
  else
    load_table(static_cast<Tab>(tab_), false, announce);
}

void ProjectWindow::select_tab(int tab) {
  tab_ = tab;
  visual_ = false;
  filter_.clear();
  on_focus();
}

// ------------------------------------------------------------------ data

std::vector<const Issue*> ProjectWindow::visible_rows() const {
  std::vector<const Issue*> out;
  for (const auto& issue : table(static_cast<Tab>(tab_)).issues) {
    const std::string haystack = issue.key + " " + issue.summary + " " + issue.status + " " + issue.assignee;
    if (ui::matches(haystack, filter_.query)) out.push_back(&issue);
  }
  return out;
}

std::vector<ProjectWindow::Column> ProjectWindow::board_columns() const {
  std::vector<Column> columns;
  for (const auto& c : config_) columns.push_back({c.name, {}});

  // Fall back to one column per distinct status when the board configuration
  // is unavailable (it needs board-admin scope on some instances).
  constexpr size_t kNoColumn = static_cast<size_t>(-1);
  for (const auto& issue : board_issues_) {
    if (!ui::matches(issue.key + " " + issue.summary + " " + issue.assignee, filter_.query)) continue;
    // An exact name, not a substring: this filter comes from a list of the people
    // actually on the board, so "Lee" must not also pick up "Lee Harper".
    if (board_assignee_ && issue.assignee != *board_assignee_) continue;

    size_t target = kNoColumn;
    for (size_t i = 0; i < config_.size() && target == kNoColumn; ++i) {
      const auto& ids = config_[i].status_ids;
      if (std::find(ids.begin(), ids.end(), issue.status_id) != ids.end()) target = i;
    }
    // Falling back to the name catches a column whose status list did not come
    // through, and groups the cards added below.
    for (size_t i = 0; i < columns.size() && target == kNoColumn; ++i)
      if (columns[i].name == issue.status) target = i;

    // A status that is not on the board — one left out of the column layout, or
    // a board whose configuration we could not read — still has to be visible.
    // Dropping the card is how a board full of issues renders as empty columns.
    if (target == kNoColumn) {
      columns.push_back({issue.status.empty() ? tr("project.board.noStatus") : issue.status, {}, true});
      target = columns.size() - 1;
    }
    columns[target].cards.push_back(&issue);
  }
  for (Column& column : columns)
    column.hidden = std::find(hidden_columns_.begin(), hidden_columns_.end(), column.name) != hidden_columns_.end();
  return columns;
}

// What the board actually draws, and what h / l walk through.
std::vector<ProjectWindow::Column> ProjectWindow::shown_columns() const {
  std::vector<Column> out;
  for (const Column& column : board_columns())
    if (!column.hidden) out.push_back(column);
  return out;
}

const Issue* ProjectWindow::current_issue() const {
  if (tab_ == TabBoard) {
    const auto columns = shown_columns();
    if (board_col_ < 0 || board_col_ >= static_cast<int>(columns.size())) return nullptr;
    const auto& cards = columns[static_cast<size_t>(board_col_)].cards;
    const int index = board_col_ < static_cast<int>(card_sel_.size()) ? card_sel_[static_cast<size_t>(board_col_)] : 0;
    if (index < 0 || index >= static_cast<int>(cards.size())) return nullptr;
    return cards[static_cast<size_t>(index)];
  }
  const auto rows = visible_rows();
  const int index = table(static_cast<Tab>(tab_)).selected;
  if (index < 0 || index >= static_cast<int>(rows.size())) return nullptr;
  return rows[static_cast<size_t>(index)];
}

bool ProjectWindow::in_visual_range(int row) const {
  if (!visual_ || tab_ == TabBoard) return false;
  const int cursor = table(static_cast<Tab>(tab_)).selected;
  return row >= std::min(visual_anchor_, cursor) && row <= std::max(visual_anchor_, cursor);
}

std::vector<const Issue*> ProjectWindow::visual_rows() const {
  std::vector<const Issue*> out;
  const auto rows = visible_rows();
  for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    if (in_visual_range(i)) out.push_back(rows[static_cast<size_t>(i)]);
  return out;
}

void ProjectWindow::open_current() {
  if (const Issue* issue = current_issue())
    ctx_.push_window(std::make_unique<TicketWindow>(ctx_, project_, *issue));
}

// ---------------------------------------------------------------- rendering

Element ProjectWindow::render_table() {
  const auto rows = visible_rows();
  const Table& t = table(static_cast<Tab>(tab_));

  Elements lines;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const Issue& issue = *rows[static_cast<size_t>(i)];
    // The gap is its own cell, not spare room inside a column: a value that fills
    // its width would otherwise run into the next column.
    auto line = hbox({
        text(" " + issue.key) | color(Color::CyanLight) | size(WIDTH, EQUAL, 14),
        text(" "),
        text(issue.type) | dim | size(WIDTH, EQUAL, 10),
        text(" "),
        text(issue.status) | color(ui::status_color(issue.status)) | size(WIDTH, EQUAL, 16),
        text(" "),
        ui::avatar(issue.assignee),
        text(" " + (issue.assignee.empty() ? tr("project.table.unassigned") : issue.assignee)) |
            size(WIDTH, EQUAL, 20),
        text(" "),
        text(issue.summary) | flex,
        text(" "),
        text(ui::relative_time(issue.updated)) | dim | size(WIDTH, EQUAL, 10),
    });
    if (in_visual_range(i)) line = line | bgcolor(Color::Blue);
    if (i == t.selected) line = line | inverted | focus;
    lines.push_back(line);
  }
  if (lines.empty())
    lines.push_back(ui::empty_hint(t.loading            ? tr("common.loading")
                                   : query_.filtered() ? tr("project.filter.noMatch")
                                                       : tr("project.table.empty")));
  else if (!t.is_last)
    lines.push_back(ui::empty_hint(tr("project.table.more")));

  auto header = hbox({
                    text(" " + tr("project.table.columns.key")) | size(WIDTH, EQUAL, 14),
                    text(" "),
                    text(tr("project.table.columns.type")) | size(WIDTH, EQUAL, 10),
                    text(" "),
                    text(tr("project.table.columns.status")) | size(WIDTH, EQUAL, 16),
                    text(" "),
                    text(tr("project.table.columns.assignee")) | size(WIDTH, EQUAL, 22),
                    text(" "),
                    text(tr("project.table.columns.summary")) | flex,
                    text(" "),
                    text(tr("project.table.columns.updated")) | size(WIDTH, EQUAL, 10),
                }) |
                bold | dim;

  // The keys are named here as on the board, since the footer cannot hold them
  // all, and a list that is quietly filtered looks like a list missing tickets.
  const std::string filters = describe_filters();
  auto query_line = hbox({
      text(" " + tr("project.sort.hint", {{"sort", describe_sort()}})) |
          (query_.sort == SortField::Updated && query_.descending ? dim : color(Color::Yellow)),
      text("   "),
      text(filters.empty() ? tr("project.filter.hint") : tr("project.filter.active", {{"filters", filters}})) |
          (filters.empty() ? dim : color(Color::Yellow)),
  });

  return vbox({query_line, header, separator(), vbox(std::move(lines)) | vscroll_indicator | yframe | flex});
}

Element ProjectWindow::render_card(const Issue& issue, bool selected) const {
  auto head = hbox({
      text(issue.key) | color(Color::CyanLight) | bold,
      filler(),
      ui::avatar(issue.assignee),
  });
  auto meta = hbox({
      text(issue.type) | dim,
      text(" · ") | dim,
      text(issue.priority.empty() ? tr("common.noValue") : issue.priority) | dim,
      filler(),
      text(ui::relative_time(issue.updated)) | dim,
  });
  auto card = vbox({head, paragraph(issue.summary), meta}) | border;
  return selected ? (card | color(Color::Green) | bold | focus) : card;
}

Element ProjectWindow::render_board() {
  if (boards_.empty())
    return vbox({ui::empty_hint(boards_loaded_ ? tr("project.board.none") : tr("project.board.loading"))}) | flex;

  const auto columns = shown_columns();
  Elements rendered;
  for (int c = 0; c < static_cast<int>(columns.size()); ++c) {
    const Column& column = columns[static_cast<size_t>(c)];
    const bool active_column = c == board_col_;
    const int selected = c < static_cast<int>(card_sel_.size()) ? card_sel_[static_cast<size_t>(c)] : 0;

    Elements cards;
    for (int i = 0; i < static_cast<int>(column.cards.size()); ++i)
      cards.push_back(render_card(*column.cards[static_cast<size_t>(i)], active_column && i == selected));
    if (cards.empty()) cards.push_back(ui::empty_hint(tr("project.board.emptyColumn")));

    // Say when a column is ours rather than the board's, so the difference from
    // Jira's own board is explained rather than puzzling.
    const std::string name =
        column.off_board ? tr("project.board.offBoard", {{"status", column.name}}) : column.name;
    auto heading = hbox({
        text(name) | (active_column ? bold : dim),
        filler(),
        text(std::to_string(column.cards.size())) | dim,
    });
    rendered.push_back(vbox({heading, separator(), vbox(std::move(cards)) | vscroll_indicator | yframe | flex}) |
                       flex);
    if (c + 1 < static_cast<int>(columns.size())) rendered.push_back(separator());
  }

  const Board& board = boards_[static_cast<size_t>(board_)];
  const size_t hidden = board_columns().size() - columns.size();
  Elements heading_cells{
      text(" " + board.name) | bold,
      text("  " + board.type) | dim,
      filler(),
  };
  // The heading is where someone wondering about the columns looks, so it names
  // the key that changes them — the footer cannot hold every hint. It also says
  // when columns are missing on purpose, otherwise a board someone filtered down
  // looks like a board that failed to load.
  if (hidden > 0)
    heading_cells.push_back(text(tr("project.board.columns.hidden", {{"count", std::to_string(hidden)}}) + "  ") |
                            color(Color::Yellow));
  else
    heading_cells.push_back(text(tr("project.board.columns.hint") + "  ") | dim);

  // Same reason as the column hint: the footer cannot fit every board key, and a
  // filter nobody can see the key for does not get used.
  const std::string assignee =
      board_assignee_ ? (board_assignee_->empty() ? tr("project.board.assignee.unassigned") : *board_assignee_)
                      : std::string();
  heading_cells.push_back(
      text(tr(assignee.empty() ? "project.board.assignee.hint" : "project.board.assignee.active",
              {{"name", assignee}}) +
           "  ") |
      (assignee.empty() ? dim : color(Color::Yellow)));
  // Always a count: "every column is empty" and "the board has no issues" look
  // identical otherwise. Once a filter hides something, the count says how much
  // of what was loaded is on screen.
  size_t on_screen = 0;
  for (const Column& column : columns) on_screen += column.cards.size();
  const bool everything = on_screen == board_issues_.size() &&
                          board_total_ <= static_cast<int>(board_issues_.size());
  heading_cells.push_back(
      text(everything ? tr("project.board.count", {{"count", std::to_string(board_issues_.size())}}) + " "
                      : tr("project.board.showing",
                           {{"shown", std::to_string(on_screen)},
                            {"total", std::to_string(std::max<int>(board_total_,
                                                                   static_cast<int>(board_issues_.size())))}}) +
                            " ") |
      dim);
  auto heading = hbox(std::move(heading_cells));

  // A board with columns but no cards is the one state that looks like a broken
  // app rather than an empty board, so it says which it is.
  Elements body{heading, separator()};
  if (board_issues_.empty() && board_total_ >= 0)
    body.push_back(ui::empty_hint(tr("project.board.noIssues")));
  body.push_back(ui::equal_columns(std::move(rendered)) | flex);
  return vbox(std::move(body));
}

Element ProjectWindow::render() {
  Elements body{
      hbox({ui::tab_bar(tab_names(), tab_), filler(), text(project_.name) | dim, text(" ")}),
      separator(),
      (tab_ == TabBoard ? render_board() : render_table()) | flex,
  };
  if (filter_.active || !filter_.query.empty())
    body.push_back(hbox({text(" /"), text(filter_.query) | bold}) | color(Color::Yellow));
  if (visual_ && tab_ != TabBoard)
    body.push_back(text(" " + tr("project.visual.bar", {{"count", std::to_string(visual_rows().size())}})) |
                   bold | color(Color::Blue));

  return ui::panel(tr("project.title", {{"key", project_.key}, {"name", project_.name}}),
                   vbox(std::move(body)), true);
}

// ------------------------------------------------------------------ events

bool ProjectWindow::on_table_event(const Event& event) {
  Table& t = table(static_cast<Tab>(tab_));
  const int count = static_cast<int>(visible_rows().size());

  if (ui::motion(event, t.selected, count)) {
    // Keep the table topped up as the cursor approaches the end.
    if (!t.is_last && t.selected >= count - 5) load_table(static_cast<Tab>(tab_), true);
    return true;
  }
  if (event == Event::Character('m')) {
    load_table(static_cast<Tab>(tab_), true);
    return true;
  }
  if (event == Event::Character('t')) {
    choose_sort();
    return true;
  }
  if (event == Event::Character('f')) {
    choose_filter();
    return true;
  }
  if (event == Event::Character('v')) {
    visual_ = !visual_;
    visual_anchor_ = t.selected;
    ctx_.set_status(tr(visual_ ? "project.visual.on" : "project.visual.off"));
    return true;
  }
  return false;
}

bool ProjectWindow::on_board_event(const Event& event) {
  if (event == Event::Character('b')) {
    choose_board();
    return true;
  }
  // Before the early return below: on a board with nothing on it these are the
  // keys worth pressing, and they used to be dead.
  if (event == Event::Character('c')) {
    choose_columns();
    return true;
  }
  if (event == Event::Character('f')) {
    choose_board_assignee();
    return true;
  }

  const auto columns = shown_columns();
  if (columns.empty()) return false;
  if (card_sel_.size() < columns.size()) card_sel_.resize(columns.size(), 0);
  board_col_ = std::clamp(board_col_, 0, static_cast<int>(columns.size()) - 1);

  const int cards = static_cast<int>(columns[static_cast<size_t>(board_col_)].cards.size());
  int& selected = card_sel_[static_cast<size_t>(board_col_)];
  selected = std::clamp(selected, 0, std::max(0, cards - 1));

  if (event == Event::Character('l') || event == Event::ArrowRight || event == Event::Tab) {
    board_col_ = (board_col_ + 1) % static_cast<int>(columns.size());
    return true;
  }
  if (event == Event::Character('h') || event == Event::ArrowLeft || event == Event::TabReverse) {
    board_col_ = (board_col_ + static_cast<int>(columns.size()) - 1) % static_cast<int>(columns.size());
    return true;
  }
  if (ui::motion(event, selected, cards)) return true;
  return false;
}

bool ProjectWindow::on_event(const Event& event) {
  if (filter_.on_event(event)) {
    table(static_cast<Tab>(tab_)).selected = 0;
    visual_ = false;
    return true;
  }
  // Before the window's own back key: Esc or q leaves the selection first.
  if (visual_ && ui::is_back(event)) {
    visual_ = false;
    ctx_.set_status(tr("project.visual.off"));
    return true;
  }

  if (event == Event::Character(']')) return select_tab((tab_ + 1) % TabCount), true;
  if (event == Event::Character('[')) return select_tab((tab_ + TabCount - 1) % TabCount), true;
  for (int i = 0; i < TabCount; ++i)
    if (event == Event::Character(static_cast<char>('1' + i))) return select_tab(i), true;

  if (tab_ == TabBoard ? on_board_event(event) : on_table_event(event)) return true;

  if (ui::is_select(event)) {
    open_current();
    return true;
  }
  if (event == Event::Character('r')) {
    reload();
    return true;
  }
  if (event == Event::Character('n')) {
    actions::create_issue(ctx_, life, project_.key, [this](const std::string&) { reload(false); });
    return true;
  }

  // The new order puts the changed tickets elsewhere, so the selection ends
  // with the refresh; a cancelled dialog leaves it in place.
  if (visual_ && tab_ != TabBoard && (event == Event::Character('s') || event == Event::Character('a'))) {
    std::vector<std::string> keys;
    for (const Issue* row : visual_rows()) keys.push_back(row->key);
    auto done = ui::guarded(life, [this] {
      visual_ = false;
      reload(false);
    });
    if (event == Event::Character('s'))
      actions::change_status_all(ctx_, life, std::move(keys), done);
    else
      actions::reassign_all(ctx_, life, std::move(keys), done);
    return true;
  }

  const Issue* issue = current_issue();
  if (!issue) return false;
  auto refresh = ui::guarded(life, [this] { reload(false); });

  if (event == Event::Character('e')) return actions::edit_summary(ctx_, life, *issue, refresh), true;
  if (event == Event::Character('a')) return actions::reassign(ctx_, life, *issue, refresh), true;
  if (event == Event::Character('s')) return actions::change_status(ctx_, life, *issue, refresh), true;
  if (event == Event::Character('o')) return actions::open_in_browser(ctx_, *issue), true;
  if (event == Event::Character('y')) return actions::copy_detail(ctx_, life, issue->key), true;
  return false;
}

std::vector<ui::KeyHelp> ProjectWindow::keys() const {
  std::vector<ui::KeyHelp> out{
      {"[ / ]", tr("project.keys.tabs")},
      {"1 … 4", tr("project.keys.jumpTab")},
  };
  if (tab_ == TabBoard) {
    out.push_back({"h / l", tr("project.keys.columns")});
    out.push_back({"j / k", tr("project.keys.cards")});
    out.push_back({"b", tr("project.keys.switchBoard")});
    out.push_back({"c", tr("project.keys.pickColumns")});
    out.push_back({"f", tr("project.keys.pickAssignee")});
  } else {
    out.push_back({"j / k", tr("project.keys.move")});
    out.push_back({"g / G, d / u", tr("project.keys.jump")});
    out.push_back({"t", tr("project.keys.sort")});
    out.push_back({"f", tr("project.keys.filterBy")});
    out.push_back({"m", tr("project.keys.more")});
    out.push_back({"v", tr("project.keys.visual")});
  }
  out.insert(out.end(), {
                            {"Enter / Space", tr("project.keys.open")},
                            {"n", tr("project.keys.create")},
                            {"e", tr("project.keys.editSummary")},
                            {"a", tr("project.keys.reassign")},
                            {"s", tr("project.keys.status")},
                            {"o", tr("project.keys.browser")},
                            {"y", tr("project.keys.copy")},
                            {"/", tr("project.keys.filter")},
                            {"r", tr("project.keys.reload")},
                            {"Esc / q", tr("project.keys.back")},
                        });
  return out;
}
