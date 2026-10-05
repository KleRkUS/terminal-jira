// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "models.hpp"
#include "ui.hpp"

// Ticket window: everything about one issue, plus the keys to change it.
class TicketWindow : public ui::Window {
 public:
  TicketWindow(ui::Context& ctx, Project project, Issue summary);

  std::string breadcrumb() const override;
  ftxui::Element render() override;
  bool on_event(const ftxui::Event& event) override;
  std::vector<ui::KeyHelp> keys() const override;
  void on_focus() override;

 private:
  // The fields the cursor walks, in the order they are drawn. Everything in
  // this list can be changed, which is what makes the cursor meaningful;
  // read-only facts like "updated" live in the heading instead.
  enum Field { FieldStatus, FieldType, FieldPriority, FieldAssignee, FieldReporter, FieldParent, FieldLabels, FieldCount };

  // `announce` is false after an edit, so the status bar keeps showing what the
  // edit did rather than the reload that follows it.
  void load(bool announce = true);

  // Opens the dialog for the highlighted field.
  void edit_field(Field field);

  std::string field_label(Field field) const;
  std::string field_value(Field field) const;

  ftxui::Element render_fields() const;
  ftxui::Element render_description() const;
  ftxui::Element render_comments() const;

  ui::Context& ctx_;
  Project project_;
  Issue issue_;  // starts as the list row, replaced by the full issue
  std::vector<Comment> comments_;
  bool detailed_ = false;
  int field_ = FieldStatus;
  int scroll_ = 0;
};
