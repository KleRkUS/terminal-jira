// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "jira_client.hpp"
#include "ui.hpp"

// Hosts the window stack, the worker thread and the modal dialogs. Windows see
// it only through ui::Context.
class App : public ui::Context {
 public:
  explicit App(JiraClient& client);
  ~App() override;

  void run();

  // A startup warning that stays on screen until the first keypress, so it
  // cannot be scrolled away by the status messages of the initial load.
  void set_notice(std::string message);

  // ui::Context
  JiraClient& jira() override { return client_; }
  void submit(std::function<void()> job) override;
  void post(std::function<void()> fn) override;
  void job_started() override;
  void job_finished() override;
  void set_status(std::string message, bool error = false) override;
  void report_error(std::string action, std::string detail) override;
  void prompt(std::string title, std::string initial,
              std::function<void(const std::string&)> on_submit) override;
  void choose(std::string title, std::vector<std::string> options, std::function<void(int)> on_pick) override;
  void check_list(std::string title, std::vector<std::string> options, std::vector<bool> checked,
                  std::function<void(const std::vector<bool>&)> on_done) override;
  void confirm(std::string question, std::function<void()> on_yes) override;
  void block(std::string message) override;
  void unblock() override;
  bool edit_externally(const std::string& initial, std::string& out) override;
  void open_in_browser(const std::string& url) override;
  bool copy_to_clipboard(const std::string& text) override;
  void push_window(std::unique_ptr<ui::Window> window) override;
  void pop_window() override;

 private:
  enum class Modal { None, Prompt, Picker, Checklist, Confirm, Help };

  void apply_pending();
  ui::Window* top();

  ftxui::Element render();
  ftxui::Element render_chrome(ftxui::Element body);
  ftxui::Element render_modal();
  ftxui::Element render_help();
  ftxui::Element render_toasts();

  bool on_event(const ftxui::Event& event);
  bool on_prompt_event(const ftxui::Event& event);
  bool on_picker_event(const ftxui::Event& event);
  bool on_checklist_event(const ftxui::Event& event);
  bool on_confirm_event(const ftxui::Event& event);

  std::vector<int> picker_matches() const;
  void close_modal();
  void worker_loop();
  // Wakes the event loop while popups are on screen so they can fade and
  // expire; the loop is otherwise idle until the next keypress.
  void toast_ticker();
  void drop_expired_toasts();

  JiraClient& client_;
  ftxui::ScreenInteractive* screen_ = nullptr;

  std::vector<std::unique_ptr<ui::Window>> stack_;
  // Window changes are deferred so a window is never destroyed while it is
  // handling an event.
  std::vector<std::unique_ptr<ui::Window>> pending_push_;
  int pending_pop_ = 0;
  bool focus_pending_ = false;

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_ = false;
  int in_flight_ = 0;

  std::string status_;
  bool status_error_ = false;
  std::string notice_;

  bool blocked_ = false;
  std::string block_message_;

  // A failed request, shown bottom right until it ages out.
  struct Toast {
    std::string action;
    std::string detail;
    std::chrono::steady_clock::time_point raised;
  };
  std::vector<Toast> toasts_;  // oldest first, which is also top to bottom

  std::thread ticker_;
  std::mutex ticker_mutex_;
  std::condition_variable ticker_wake_;
  bool ticker_stop_ = false;
  std::atomic<bool> toasts_visible_{false};

  Modal modal_ = Modal::None;
  std::string modal_title_;
  std::function<void(const std::string&)> on_submit_;
  std::function<void(int)> on_pick_;
  std::function<void(const std::vector<bool>&)> on_check_;
  std::function<void()> on_yes_;

  std::string prompt_text_;
  int prompt_cursor_ = 0;
  ftxui::Component prompt_input_;

  std::vector<std::string> picker_options_;
  ui::Filter picker_filter_;
  int picker_selected_ = 0;

  // The checklist reuses the picker's options and cursor; only the boxes are its
  // own, so there is one list rendering and one set of motion keys.
  std::vector<bool> checked_;
};
