// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows/ticket_window.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "actions.hpp"
#include "translations.hpp"

using namespace ftxui;
using translations::tr;

namespace {

// `when_empty` differs per block, so the caller names it.
Elements as_lines(const std::string& text_block, const std::string& when_empty) {
  Elements lines;
  std::istringstream in(text_block);
  std::string line;
  while (std::getline(in, line)) lines.push_back(paragraph(line));
  if (lines.empty()) lines.push_back(ui::empty_hint(when_empty));
  return lines;
}

}  // namespace

TicketWindow::TicketWindow(ui::Context& ctx, Project project, Issue summary)
    : ctx_(ctx), project_(std::move(project)), issue_(std::move(summary)) {}

std::string TicketWindow::breadcrumb() const {
  return tr("ticket.breadcrumb", {{"key", project_.key}, {"issue", issue_.key}});
}

void TicketWindow::on_focus() { load(); }

void TicketWindow::load(bool announce) {
  const std::string key = issue_.key;
  if (announce) ctx_.set_status(tr("ticket.message.loading", {{"key", key}}));

  // Issue fields and comments are separate endpoints; fetch both together.
  struct Snapshot {
    Issue issue;
    std::vector<Comment> comments;
  };
  ui::async(
      ctx_, life, tr("ticket.action.load", {{"key", key}}),
      [this, key] {
        Snapshot s;
        s.issue = ctx_.jira().issue_detail(key);
        s.comments = ctx_.jira().comments(key);
        return s;
      },
      [this, announce](Snapshot s) {
        issue_ = std::move(s.issue);
        comments_ = std::move(s.comments);
        detailed_ = true;
        if (announce)
          ctx_.set_status(tr("ticket.message.loaded", {{"key", issue_.key}, {"status", issue_.status}}));
      });
}

// ---------------------------------------------------------------- rendering

std::string TicketWindow::field_label(Field field) const {
  switch (field) {
    case FieldStatus: return tr("ticket.status");
    case FieldType: return tr("ticket.type");
    case FieldPriority: return tr("ticket.priority");
    case FieldAssignee: return tr("ticket.assignee");
    case FieldReporter: return tr("ticket.reporter");
    case FieldParent: return tr("ticket.parent");
    case FieldLabels: return tr("ticket.labels");
    case FieldCount: break;
  }
  return "";
}

std::string TicketWindow::field_value(Field field) const {
  switch (field) {
    case FieldStatus: return issue_.status;
    case FieldType: return issue_.type;
    case FieldPriority: return issue_.priority;
    case FieldAssignee:
      return issue_.assignee.empty() ? tr("ticket.assignee.unassigned") : issue_.assignee;
    case FieldReporter: return issue_.reporter;
    case FieldParent: return issue_.parent;
    case FieldLabels: {
      std::string labels;
      for (const auto& label : issue_.labels) labels += (labels.empty() ? "" : ", ") + label;
      return labels;
    }
    case FieldCount: break;
  }
  return "";
}

Element TicketWindow::render_fields() const {
  Elements rows;
  for (int i = 0; i < FieldCount; ++i) {
    const Field field = static_cast<Field>(i);
    const bool selected = i == field_;
    const std::string value = field_value(field);

    auto label = text(" " + field_label(field)) | size(WIDTH, EQUAL, 13);
    auto row = hbox({selected ? label : label | dim, text(value.empty() ? tr("common.noValue") : value) | flex});
    // The status keeps its colour, which is most of what the field is for.
    if (field == FieldStatus) row = row | color(ui::status_color(issue_.status));
    rows.push_back(selected ? (row | inverted) : row);
  }
  return vbox(std::move(rows));
}

Element TicketWindow::render_description() const {
  return ui::panel(tr("ticket.description"),
                   vbox(as_lines(issue_.description, tr("ticket.description.empty"))) | vscroll_indicator |
                       yframe | flex,
                   false);
}

Element TicketWindow::render_comments() const {
  Elements blocks;
  for (const auto& comment : comments_) {
    blocks.push_back(hbox({
        ui::avatar(comment.author),
        text(" " + comment.author) | bold,
        text("  " + ui::relative_time(comment.created)) | dim,
    }));
    for (auto& line : as_lines(comment.body, tr("common.noValue"))) blocks.push_back(std::move(line));
    blocks.push_back(separatorEmpty());
  }
  if (blocks.empty())
    blocks.push_back(ui::empty_hint(detailed_ ? tr("ticket.comments.empty") : tr("common.loading")));

  return ui::panel(tr("ticket.comments", {{"count", std::to_string(comments_.size())}}),
                   vbox(std::move(blocks)) | vscroll_indicator | yframe | flex, false);
}

Element TicketWindow::render() {
  auto heading = vbox({
      hbox({
          text(" " + issue_.key) | bold | color(Color::CyanLight),
          text("  "),
          text(issue_.summary) | bold | flex,
          text(tr("ticket.updated") + " " + ui::relative_time(issue_.updated) + " ") | dim,
      }),
      separatorEmpty(),
      render_fields(),
  });

  // The scroll offset is applied by nudging focus down the body.
  auto body = vbox({
                  render_description() | flex,
                  render_comments() | flex,
              }) |
              focusPositionRelative(0.0f, static_cast<float>(scroll_) / 100.0f) | frame | flex;

  return ui::panel(issue_.key, vbox({heading, separator(), body | flex}), true);
}

// ------------------------------------------------------------------ events

// Each field is a different endpoint and a different dialog, so the window only
// decides which one the cursor is on.
void TicketWindow::edit_field(Field field) {
  auto refresh = ui::guarded(life, [this] { load(false); });
  switch (field) {
    case FieldStatus: return actions::change_status(ctx_, life, issue_, refresh);
    case FieldType: return actions::change_type(ctx_, life, project_.key, issue_, refresh);
    case FieldPriority: return actions::change_priority(ctx_, life, issue_, refresh);
    case FieldAssignee: return actions::reassign(ctx_, life, issue_, refresh);
    case FieldReporter: return actions::change_reporter(ctx_, life, issue_, refresh);
    case FieldParent: return actions::change_parent(ctx_, life, project_.key, issue_, refresh);
    case FieldLabels:
      // Editing the labels needs the full list, which arrives with the detail
      // request; the list row only carries what the table shows.
      if (!detailed_)
        return ctx_.set_status(tr("ticket.message.stillLoading", {{"key", issue_.key}}), true);
      return actions::edit_labels(ctx_, life, issue_, refresh);
    case FieldCount: break;
  }
}

bool TicketWindow::on_event(const Event& event) {
  // j/k walk the fields, as they do in every other list in the app; the body
  // scrolls with the shifted pair.
  if (event == Event::Character('j') || event == Event::ArrowDown)
    return field_ = std::min(field_ + 1, FieldCount - 1), true;
  if (event == Event::Character('k') || event == Event::ArrowUp)
    return field_ = std::max(field_ - 1, 0), true;
  if (ui::is_select(event)) return edit_field(static_cast<Field>(field_)), true;

  if (event == Event::Character('J')) {
    scroll_ = std::min(scroll_ + 5, 100);
    return true;
  }
  if (event == Event::Character('K')) {
    scroll_ = std::max(scroll_ - 5, 0);
    return true;
  }
  if (event == Event::Character('g')) return scroll_ = 0, true;
  if (event == Event::Character('G')) return scroll_ = 100, true;

  auto refresh = ui::guarded(life, [this] { load(false); });
  if (event == Event::Character('r')) return load(), true;
  if (event == Event::Character('e')) return actions::edit_summary(ctx_, life, issue_, refresh), true;
  if (event == Event::Character('a')) return actions::reassign(ctx_, life, issue_, refresh), true;
  if (event == Event::Character('s')) return actions::change_status(ctx_, life, issue_, refresh), true;
  if (event == Event::Character('o')) return actions::open_in_browser(ctx_, issue_), true;
  if (event == Event::Character('y')) return actions::copy_detail(ctx_, issue_), true;

  // Editing the description or writing a comment needs the full issue body,
  // which only arrives with the detail request.
  if (event == Event::Character('E')) {
    if (!detailed_) return ctx_.set_status(tr("ticket.message.stillLoading", {{"key", issue_.key}}), true), true;
    actions::edit_description(ctx_, life, issue_, refresh);
    return true;
  }
  if (event == Event::Character('c')) {
    actions::add_comment(ctx_, life, issue_, refresh);
    return true;
  }
  return false;
}

std::vector<ui::KeyHelp> TicketWindow::keys() const {
  return {
      {"j / k", tr("ticket.keys.fields")},
      {"Enter / Space", tr("ticket.keys.edit")},
      {"J / K", tr("ticket.keys.scroll")},
      {"g / G", tr("ticket.keys.ends")},
      {"e", tr("ticket.keys.editSummary")},
      {"E", tr("ticket.keys.editDescription")},
      {"a", tr("ticket.keys.reassign")},
      {"s", tr("ticket.keys.status")},
      {"c", tr("ticket.keys.comment")},
      {"o", tr("ticket.keys.browser")},
      {"y", tr("ticket.keys.copy")},
      {"r", tr("ticket.keys.reload")},
      {"Esc / q", tr("ticket.keys.back")},
  };
}
