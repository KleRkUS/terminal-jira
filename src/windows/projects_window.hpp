// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "models.hpp"
#include "ui.hpp"

// Root window: pick a project to work in.
class ProjectsWindow : public ui::Window {
 public:
  explicit ProjectsWindow(ui::Context& ctx);

  std::string breadcrumb() const override;
  ftxui::Element render() override;
  bool on_event(const ftxui::Event& event) override;
  std::vector<ui::KeyHelp> keys() const override;
  void on_focus() override;

 private:
  void load();
  void open_selected();
  std::vector<const Project*> visible() const;

  ui::Context& ctx_;
  std::vector<Project> projects_;
  ui::Filter filter_;
  int selected_ = 0;
  bool loaded_ = false;
};
