// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "jira_client.hpp"

namespace ui {

class Window;

using KeyHelp = std::pair<std::string, std::string>;

// Services a window may use. Implemented by App; passed to every window so the
// windows never touch the screen or the worker thread directly.
class Context {
 public:
  virtual ~Context() = default;

  virtual JiraClient& jira() = 0;

  // Runs `job` on the worker thread; `fn` on the UI thread. Prefer the async()
  // helper below over calling these directly.
  virtual void submit(std::function<void()> job) = 0;
  virtual void post(std::function<void()> fn) = 0;
  virtual void job_started() = 0;
  virtual void job_finished() = 0;

  virtual void set_status(std::string message, bool error = false) = 0;

  // A failed request. `action` names what the user was doing, in words they
  // would recognise ("Move ENG-12"); `detail` is the raw status and body.
  // Shown as a transient popup, so it never interrupts what they are doing.
  virtual void report_error(std::string action, std::string detail) = 0;

  // Modal dialogs. The callback runs on the UI thread; cancelling runs nothing.
  virtual void prompt(std::string title, std::string initial,
                      std::function<void(const std::string&)> on_submit) = 0;
  virtual void choose(std::string title, std::vector<std::string> options,
                      std::function<void(int)> on_pick) = 0;
  // A checklist: several answers at once. `checked` starts the boxes off and the
  // callback receives one flag per option, in the order they were given.
  virtual void check_list(std::string title, std::vector<std::string> options, std::vector<bool> checked,
                          std::function<void(const std::vector<bool>&)> on_done) = 0;
  virtual void confirm(std::string question, std::function<void()> on_yes) = 0;

  // Takes the keyboard away and shows `message` over the screen until
  // unblock(). For a change sent ticket by ticket: moving the selection or
  // leaving the window halfway through would leave the user unsure what was
  // changed. Calling it again while blocked only replaces the message. Ctrl-C
  // still quits.
  virtual void block(std::string message) = 0;
  virtual void unblock() = 0;

  // Suspends the TUI, runs $EDITOR on `initial`, and returns the edited text.
  // Returns false if the editor failed or the text came back unchanged.
  virtual bool edit_externally(const std::string& initial, std::string& out) = 0;
  virtual void open_in_browser(const std::string& url) = 0;

  // Puts `text` on the system clipboard. False when nothing would take it. A
  // terminal asked to hold the text over OSC 52 may still ignore it, which it
  // gives us no way to find out, so this is "accepted" rather than "arrived".
  virtual bool copy_to_clipboard(const std::string& text) = 0;

  virtual void push_window(std::unique_ptr<Window> window) = 0;
  virtual void pop_window() = 0;
};

// A full-screen view. Exactly one window is visible at a time; it owns its own
// layout and keymap, and unhandled keys fall through to the application.
class Window {
 public:
  virtual ~Window() = default;

  virtual std::string breadcrumb() const = 0;
  virtual ftxui::Element render() = 0;
  virtual bool on_event(const ftxui::Event& event) = 0;
  virtual std::vector<KeyHelp> keys() const = 0;

  // Called when this window becomes visible, including on return from a child.
  virtual void on_focus() {}

  // Guards async callbacks: they are dropped if the window is gone.
  std::shared_ptr<int> life = std::make_shared<int>(0);
};

// Formats a failed request as "HTTP 403 · {body}", or just the transport error
// when the request never reached Jira.
std::string describe_failure(long status, const std::string& body);

// Runs `work` on the worker thread, then `done(result)` on the UI thread.
//
// `action` is what the user was trying to do, phrased for them rather than for a
// log: it becomes the heading of the error popup when the request fails. It
// comes from the string catalog like any other text — see translations.hpp. On
// failure `done` is skipped; it is also skipped if the owning window has been
// closed in the meantime. The life token is held weakly throughout so a pending
// callback never keeps a window alive.
// `on_error` also runs on the UI thread, after the popup is raised, and only
// while the window is alive; use it to reset loading state.
template <typename Work, typename Done, typename Fail>
void async(Context& ctx, std::weak_ptr<int> weak, std::string action, Work work, Done done, Fail on_error) {
  using T = std::invoke_result_t<Work>;
  ctx.job_started();
  ctx.submit([&ctx, weak, action = std::move(action), work = std::move(work), done = std::move(done),
              on_error = std::move(on_error)]() mutable {
    auto value = std::make_shared<T>();
    std::string failure;
    try {
      *value = work();
    } catch (const JiraError& ex) {
      failure = describe_failure(ex.status, ex.body);
    } catch (const std::exception& ex) {
      failure = ex.what();
    }
    ctx.post([&ctx, weak, value, failure, action = std::move(action), done = std::move(done),
              on_error = std::move(on_error)]() mutable {
      ctx.job_finished();
      if (!failure.empty()) {
        ctx.report_error(std::move(action), std::move(failure));
        if (!weak.expired()) on_error();
        return;
      }
      if (weak.expired()) return;
      done(std::move(*value));
    });
  });
}

template <typename Work, typename Done>
void async(Context& ctx, std::weak_ptr<int> weak, std::string action, Work work, Done done) {
  async(ctx, std::move(weak), std::move(action), std::move(work), std::move(done), [] {});
}

// Wraps a callback so that it is dropped if its window has been closed. Dialog
// callbacks can outlive the window that opened them.
template <typename F>
std::function<void()> guarded(std::weak_ptr<int> weak, F fn) {
  return [weak, fn = std::move(fn)] {
    if (!weak.expired()) fn();
  };
}

// Incremental filter shared by every list and the picker modal.
struct Filter {
  bool active = false;
  std::string query;

  void clear() {
    active = false;
    query.clear();
  }
  // Consumes typing keys while active. Returns true if the event was used.
  bool on_event(const ftxui::Event& event);
};

bool matches(const std::string& haystack, const std::string& query);

// Applies vim-style motions (j/k, arrows, g/G, d/u, page keys) to `index`,
// keeping it inside [0, size). Returns true if the event was consumed.
bool motion(const ftxui::Event& event, int& index, int size);

// Confirms the highlighted item: a project, a ticket, or an option in a dialog.
// Check any active text filter first, so a typed space reaches the query.
bool is_select(const ftxui::Event& event);

// Leaves the current window or dialog. Likewise check text input first, since
// `q` is an ordinary character while typing.
bool is_back(const ftxui::Event& event);

// Render helpers.
ftxui::Element panel(const std::string& title, ftxui::Element content, bool active);
ftxui::Element simple_list(const std::vector<std::string>& lines, int selected, bool active);
ftxui::Element tab_bar(const std::vector<std::string>& names, int selected);
// Side-by-side columns of identical width. hbox() grows each child from the
// width its own content asks for, which makes a board column of long summaries
// wider than its neighbours; this divides the space evenly instead. Children
// that cannot grow — a separator between two columns — keep their own width and
// are not counted as columns.
ftxui::Element equal_columns(ftxui::Elements children);
ftxui::Element key_value(const std::string& key, const std::string& value, int key_width = 12);
ftxui::Element empty_hint(const std::string& text);
ftxui::Color status_color(const std::string& status);
ftxui::Element avatar(const std::string& display_name);
std::string relative_time(const std::string& jira_timestamp);

}  // namespace ui
