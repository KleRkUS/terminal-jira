// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows/projects_window.hpp"

#include <algorithm>
#include <memory>

#include "translations.hpp"
#include "windows/project_window.hpp"

using namespace ftxui;
using translations::tr;

ProjectsWindow::ProjectsWindow(ui::Context& ctx) : ctx_(ctx) {}

std::string ProjectsWindow::breadcrumb() const { return tr("projects.breadcrumb"); }

void ProjectsWindow::on_focus() {
  if (!loaded_) load();
}

void ProjectsWindow::load() {
  loaded_ = true;
  ctx_.set_status(tr("projects.status.loading"));
  ui::async(
      ctx_, life, tr("projects.action.load"), [this] { return ctx_.jira().projects(); },
      [this](std::vector<Project> projects) {
        projects_ = std::move(projects);
        selected_ = 0;
        ctx_.set_status(tr("projects.status.loaded", {{"count", std::to_string(projects_.size())}}));
      });
}

std::vector<const Project*> ProjectsWindow::visible() const {
  std::vector<const Project*> out;
  for (const auto& p : projects_)
    if (ui::matches(p.key + " " + p.name, filter_.query)) out.push_back(&p);
  return out;
}

void ProjectsWindow::open_selected() {
  const auto shown = visible();
  if (selected_ < 0 || selected_ >= static_cast<int>(shown.size())) return;
  ctx_.push_window(std::make_unique<ProjectWindow>(ctx_, *shown[static_cast<size_t>(selected_)]));
}

Element ProjectsWindow::render() {
  const auto shown = visible();

  Elements rows;
  for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
    const Project& p = *shown[static_cast<size_t>(i)];
    auto row = hbox({
        text(" " + p.key) | bold | size(WIDTH, EQUAL, 14),
        text(p.name) | flex,
        ui::avatar(p.lead),
        text(" " + p.lead) | dim | size(WIDTH, EQUAL, 22),
    });
    if (i == selected_) row = row | inverted | focus;
    rows.push_back(row);
  }
  if (rows.empty())
    rows.push_back(ui::empty_hint(loaded_ && projects_.empty() ? tr("projects.none") : tr("projects.noMatch")));

  auto header = hbox({
                    text(" " + tr("projects.columns.key")) | size(WIDTH, EQUAL, 14),
                    text(tr("projects.columns.name")) | flex,
                    text(tr("projects.columns.lead")) | size(WIDTH, EQUAL, 24),
                }) |
                bold | dim;

  Elements body{header, separator(), vbox(std::move(rows)) | vscroll_indicator | yframe | flex};
  if (filter_.active) body.push_back(hbox({text(" /"), text(filter_.query) | bold}) | color(Color::Yellow));

  const std::string title =
      projects_.empty() ? tr("projects.title")
                        : tr("projects.title.counted", {{"shown", std::to_string(shown.size())},
                                                        {"total", std::to_string(projects_.size())}});
  return ui::panel(title, vbox(std::move(body)), true);
}

bool ProjectsWindow::on_event(const Event& event) {
  const int count = static_cast<int>(visible().size());

  if (filter_.on_event(event)) {
    selected_ = 0;
    return true;
  }
  if (ui::motion(event, selected_, count)) return true;
  if (ui::is_select(event)) {
    open_selected();
    return true;
  }
  if (event == Event::Character('r')) {
    load();
    return true;
  }
  return false;
}

std::vector<ui::KeyHelp> ProjectsWindow::keys() const {
  return {
      {"j / k", tr("projects.keys.move")},
      {"g / G", tr("projects.keys.ends")},
      {"d / u", tr("projects.keys.halfPage")},
      {"/", tr("projects.keys.filter")},
      {"Enter / Space", tr("projects.keys.open")},
      {"r", tr("projects.keys.reload")},
      {"?", tr("projects.keys.help")},
      {"q / Esc", tr("projects.keys.quit")},
  };
}
