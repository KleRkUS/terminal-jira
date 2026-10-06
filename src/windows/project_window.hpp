// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "models.hpp"
#include "ui.hpp"

// Project window: tabs of issue tables plus the board view for this project.
class ProjectWindow : public ui::Window {
 public:
  ProjectWindow(ui::Context& ctx, Project project);

  std::string breadcrumb() const override;
  ftxui::Element render() override;
  bool on_event(const ftxui::Event& event) override;
  std::vector<ui::KeyHelp> keys() const override;
  void on_focus() override;

 private:
  enum Tab { TabMine, TabOpen, TabAll, TabBoard, TabCount };

  // An issue table, loaded one page at a time.
  struct Table {
    std::vector<Issue> issues;
    std::string next_token;
    bool is_last = true;
    bool loaded = false;
    bool loading = false;
    int selected = 0;
  };

  // Columns of the board view, each holding the issues currently in it.
  struct Column {
    std::string name;
    std::vector<const Issue*> cards;
    // True for a column the board itself does not have: see board_columns().
    bool off_board = false;
    // Hidden columns are built like any other, so the chooser can list them and
    // their cards are not lost; only the rendering and the cursor skip them.
    bool hidden = false;
  };

  // Tab labels come from the string catalog, so the bar, the status messages
  // and the popup labels cannot drift apart.
  static std::vector<std::string> tab_names();
  static std::string tab_name(Tab tab);

  void select_tab(int tab);
  // `announce` is false for refreshes that follow an edit, so the status bar
  // keeps showing what the edit did.
  void reload(bool announce = true);

  void load_table(Tab tab, bool append, bool announce = true);
  void load_boards();
  void load_board_issues(bool announce = true);
  void choose_board();
  void choose_columns();
  void choose_board_assignee();

  std::string jql_for(Tab tab) const;
  Table& table(Tab tab) { return tables_[static_cast<size_t>(tab)]; }
  const Table& table(Tab tab) const { return tables_[static_cast<size_t>(tab)]; }
  std::vector<const Issue*> visible_rows() const;
  std::vector<Column> board_columns() const;
  std::vector<Column> shown_columns() const;
  const Issue* current_issue() const;
  void open_current();

  ftxui::Element render_table();
  ftxui::Element render_board();
  ftxui::Element render_card(const Issue& issue, bool selected) const;

  bool on_table_event(const ftxui::Event& event);
  bool on_board_event(const ftxui::Event& event);

  ui::Context& ctx_;
  Project project_;
  int tab_ = TabMine;
  ui::Filter filter_;
  Table tables_[TabCount];

  std::vector<Board> boards_;
  std::vector<BoardColumn> config_;
  std::vector<Issue> board_issues_;
  int board_ = -1;      // index into boards_
  int board_col_ = 0;   // selected column
  std::vector<int> card_sel_;
  // Hidden columns are remembered by name, so they stay hidden across a reload
  // and a column that comes back keeps its state.
  std::vector<std::string> hidden_columns_;
  // Unset shows every card; empty means the cards nobody is assigned to.
  std::optional<std::string> board_assignee_;
  bool boards_loaded_ = false;
  int board_total_ = -1;  // -1 until the board has loaded
};
