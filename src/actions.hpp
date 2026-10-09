// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "models.hpp"
#include "ui.hpp"

// Ticket mutations shared by the project and ticket windows. Each one drives
// its own dialogs, reports progress through the status bar, and calls
// `on_changed` once Jira has accepted the change.
namespace actions {

using Done = std::function<void()>;
using Life = std::weak_ptr<int>;  // held weakly: a dialog may outlive its window

void create_issue(ui::Context& ctx, const Life& life, const std::string& project_key,
                  std::function<void(const std::string& new_key)> on_created);
void edit_summary(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void edit_description(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void reassign(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void change_status(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);

// The same two changes for several tickets at once. There is no bulk endpoint
// that both Jira Cloud and Server offer, so it is one request per ticket, sent
// in turn with the interface blocked until the last answer is in. Each ticket is
// asked first what it allows, and only the choices every ticket shares are
// offered. A ticket that fails is reported and the rest still go through.
void change_status_all(ui::Context& ctx, const Life& life, std::vector<std::string> keys, Done on_changed);
void reassign_all(ui::Context& ctx, const Life& life, std::vector<std::string> keys, Done on_changed);

// The remaining editable fields of a ticket. Each opens a picker filled from
// Jira; `project_key` is needed where the choices belong to the project rather
// than to the issue.
void change_type(ui::Context& ctx, const Life& life, const std::string& project_key, const Issue& issue,
                 Done on_changed);
void change_priority(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void change_reporter(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void change_parent(ui::Context& ctx, const Life& life, const std::string& project_key, const Issue& issue,
                   Done on_changed);
void edit_labels(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void add_comment(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed);
void open_in_browser(ui::Context& ctx, const Issue& issue);

// Offers the ticket's identifier, name and description, and puts the one chosen
// on the clipboard. The description is only there once the detail request has
// come back; before that it is empty, and says so.
void copy_detail(ui::Context& ctx, const Issue& issue);

// The list and the board only carry the fields they display, so this loads the
// issue first and then offers the same choice.
void copy_detail(ui::Context& ctx, const Life& life, const std::string& issue_key);

}  // namespace actions
