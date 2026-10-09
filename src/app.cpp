// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include "platform.hpp"
#include "translations.hpp"
#include "windows/projects_window.hpp"

using namespace ftxui;
using translations::tr;

namespace {

// Popups live for four seconds and fade over the last part of it.
constexpr auto kToastLifetime = std::chrono::milliseconds(4000);
constexpr size_t kMaxToasts = 5;
constexpr int kToastWidth = 54;
constexpr size_t kToastDetailLimit = 220;

}  // namespace

App::App(JiraClient& client) : client_(client) {
  InputOption option;
  option.multiline = false;
  option.cursor_position = &prompt_cursor_;
  prompt_input_ = Input(&prompt_text_, "", option);

  worker_ = std::thread([this] { worker_loop(); });
}

App::~App() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void App::run() {
  auto screen = ScreenInteractive::Fullscreen();
  screen_ = &screen;

  // Created here and joined below, so the thread can only ever touch a screen
  // that is alive.
  ticker_ = std::thread([this] { toast_ticker(); });

  // Queued rather than pushed directly: the first window's on_focus() starts a
  // request, and results can only be posted once the loop is running.
  push_window(std::make_unique<ProjectsWindow>(*this));

  auto root = Renderer(prompt_input_, [this] { return render(); });
  root = CatchEvent(root, [this](Event event) { return on_event(event); });
  screen.Loop(root);

  {
    std::lock_guard<std::mutex> lock(ticker_mutex_);
    ticker_stop_ = true;
  }
  ticker_wake_.notify_all();
  ticker_.join();
  screen_ = nullptr;
}

void App::toast_ticker() {
  using namespace std::chrono_literals;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(ticker_mutex_);
      ticker_wake_.wait_for(lock, 100ms, [this] { return ticker_stop_; });
      if (ticker_stop_) return;
    }
    if (toasts_visible_.load()) screen_->PostEvent(Event::Custom);
  }
}

// ------------------------------------------------------------- worker thread

void App::worker_loop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();  // Jira calls are serialized here: one curl handle, one thread.
  }
}

void App::submit(std::function<void()> job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void App::post(std::function<void()> fn) {
  if (!screen_) return;
  screen_->Post(Closure(std::move(fn)));
  // FTXUI only invalidates the frame for events, not for posted closures, so a
  // result arriving from the worker would not be drawn until the next keypress.
  screen_->PostEvent(Event::Custom);
}

void App::job_started() { ++in_flight_; }

void App::job_finished() {
  if (in_flight_ > 0) --in_flight_;
}

// ------------------------------------------------------------- window stack

ui::Window* App::top() { return stack_.empty() ? nullptr : stack_.back().get(); }

void App::push_window(std::unique_ptr<ui::Window> window) { pending_push_.push_back(std::move(window)); }

void App::pop_window() { ++pending_pop_; }

void App::apply_pending() {
  bool changed = false;
  while (pending_pop_ > 0 && !stack_.empty()) {
    stack_.pop_back();
    --pending_pop_;
    changed = true;
  }
  pending_pop_ = 0;
  for (auto& window : pending_push_) {
    stack_.push_back(std::move(window));
    changed = true;
  }
  pending_push_.clear();

  if ((changed || focus_pending_) && top()) {
    focus_pending_ = false;
    top()->on_focus();
  }
  if (stack_.empty() && screen_) screen_->ExitLoopClosure()();
}

// ------------------------------------------------------------------ status

void App::set_status(std::string message, bool error) {
  status_ = std::move(message);
  status_error_ = error;
}

void App::set_notice(std::string message) { notice_ = std::move(message); }

void App::report_error(std::string action, std::string detail) {
  // Newest last: the stack is drawn top to bottom, so a new popup appears
  // beneath the ones already there.
  toasts_.push_back({std::move(action), std::move(detail), std::chrono::steady_clock::now()});
  if (toasts_.size() > kMaxToasts) toasts_.erase(toasts_.begin());
  toasts_visible_.store(true);
}

void App::drop_expired_toasts() {
  const auto now = std::chrono::steady_clock::now();
  const auto expired = [&](const Toast& toast) { return now - toast.raised >= kToastLifetime; };
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), expired), toasts_.end());
  toasts_visible_.store(!toasts_.empty());
}

// ------------------------------------------------------------------ modals

void App::prompt(std::string title, std::string initial, std::function<void(const std::string&)> on_submit) {
  modal_ = Modal::Prompt;
  modal_title_ = std::move(title);
  prompt_text_ = std::move(initial);
  prompt_cursor_ = static_cast<int>(prompt_text_.size());
  on_submit_ = std::move(on_submit);
}

void App::choose(std::string title, std::vector<std::string> options, std::function<void(int)> on_pick) {
  modal_ = Modal::Picker;
  modal_title_ = std::move(title);
  picker_options_ = std::move(options);
  picker_filter_.clear();
  picker_selected_ = 0;
  on_pick_ = std::move(on_pick);
}

void App::check_list(std::string title, std::vector<std::string> options, std::vector<bool> checked,
                     std::function<void(const std::vector<bool>&)> on_done) {
  modal_ = Modal::Checklist;
  modal_title_ = std::move(title);
  picker_options_ = std::move(options);
  picker_filter_.clear();
  picker_selected_ = 0;
  checked_ = std::move(checked);
  checked_.resize(picker_options_.size(), false);
  on_check_ = std::move(on_done);
}

void App::confirm(std::string question, std::function<void()> on_yes) {
  modal_ = Modal::Confirm;
  modal_title_ = std::move(question);
  on_yes_ = std::move(on_yes);
}

void App::block(std::string message) {
  blocked_ = true;
  block_message_ = std::move(message);
}

void App::unblock() {
  blocked_ = false;
  block_message_.clear();
}

void App::close_modal() {
  modal_ = Modal::None;
  modal_title_.clear();
  on_submit_ = nullptr;
  on_pick_ = nullptr;
  on_check_ = nullptr;
  on_yes_ = nullptr;
  picker_options_.clear();
  picker_filter_.clear();
  checked_.clear();
}

std::vector<int> App::picker_matches() const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(picker_options_.size()); ++i)
    if (ui::matches(picker_options_[static_cast<size_t>(i)], picker_filter_.query)) out.push_back(i);
  return out;
}

// --------------------------------------------------------- external programs

bool App::edit_externally(const std::string& initial, std::string& out) {
  if (!screen_) return false;

  // The editor has to own the screen, so the terminal is restored for the call
  // and the platform module only has to run the program.
  platform::EditedText edited;
  screen_->WithRestoredIO([&] { edited = platform::edit_text(initial); })();

  switch (edited.status) {
    case platform::EditStatus::CannotWrite:
      return set_status(tr("app.editor.cannotWrite", {{"path", edited.path}}), true), false;
    case platform::EditStatus::Failed:
      return set_status(tr("app.editor.failed", {{"code", std::to_string(edited.code)}}), true), false;
    case platform::EditStatus::Unchanged:
      return set_status(tr("app.editor.unchanged")), false;
    case platform::EditStatus::Ok:
      out = std::move(edited.text);
      return true;
  }
  return false;
}

bool App::copy_to_clipboard(const std::string& text) { return platform::copy_to_clipboard(text); }

void App::open_in_browser(const std::string& url) {
  if (platform::open_in_browser(url))
    set_status(tr("app.browser.opened", {{"url", url}}));
  else
    set_status(tr("app.browser.failed", {{"url", url}}), true);
}

// ---------------------------------------------------------------- rendering

Element App::render_toasts() {
  drop_expired_toasts();
  if (toasts_.empty()) return emptyElement();

  const auto now = std::chrono::steady_clock::now();
  Elements stack;
  for (const Toast& toast : toasts_) {
    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - toast.raised);
    // Hold at full strength for the first half, then fade towards the
    // background over the rest: a terminal has no alpha, so the colour itself
    // is what fades.
    const float elapsed = static_cast<float>(age.count()) / kToastLifetime.count();
    const float fade = std::clamp((elapsed - 0.5f) * 2.0f, 0.0f, 1.0f);
    const Color heading = Color::Interpolate(fade, Color::Red, Color::GrayDark);
    const Color body = Color::Interpolate(fade, Color::White, Color::GrayDark);

    std::string detail = toast.detail;
    if (detail.size() > kToastDetailLimit) detail = detail.substr(0, kToastDetailLimit) + "…";

    Element popup = vbox({
                        hbox({
                            text("✕ ") | color(heading) | bold,
                            text(toast.action) | color(heading) | bold | flex,
                        }),
                        paragraph(detail) | color(body),
                    }) |
                    border | color(heading) | size(WIDTH, EQUAL, kToastWidth) | clear_under;
    // Colour alone fades towards grey, which still reads as present on a light
    // terminal; dimming the last moments gives the exit a visible final step.
    if (fade > 0.85f) popup = popup | dim;
    stack.push_back(std::move(popup));
  }

  // Bottom right, with a column of margin so the border is not flush with the
  // edge. Overlaid on the body only, so it can never hide the status bar.
  return vbox({filler(), hbox({filler(), vbox(std::move(stack)), text("  ")}), text("")});
}

Element App::render() {
  apply_pending();

  Element body = top() ? top()->render() : text(tr("app.noWindow")) | center;
  if (!toasts_.empty()) body = dbox({std::move(body), render_toasts()});

  Element screen = render_chrome(std::move(body));
  if (blocked_)
    return dbox({std::move(screen) | dim,
                 window(text(" " + tr("app.blocked.title") + " ") | bold,
                        vbox({text(" " + block_message_ + " "), separator(),
                              text(" " + tr("app.blocked.footer") + " ") | dim})) |
                     size(WIDTH, GREATER_THAN, 48) | clear_under | center});
  if (modal_ == Modal::None) return screen;
  return dbox({std::move(screen), render_modal()});
}

Element App::render_chrome(Element body) {
  Elements crumbs{
      text(" " + tr("app.brand") + " ") | bold | inverted,
      text(" " + (top() ? top()->breadcrumb() : std::string())) | dim,
      filler(),
  };
  if (in_flight_ > 0) crumbs.push_back(text(tr("app.working") + " ") | color(Color::Yellow));
  if (stack_.size() > 1) crumbs.push_back(text(tr("app.goBack") + " ") | dim);

  Elements footer;
  if (!notice_.empty())
    footer.push_back(hbox({
        text(" ! ") | bold | bgcolor(Color::Yellow) | color(Color::Black),
        text(" " + notice_) | color(Color::Yellow) | flex,
        text(tr("app.notice.dismiss") + " ") | dim,
    }));
  if (!status_.empty()) {
    auto line = text(" " + status_);
    footer.push_back(status_error_ ? (line | color(Color::Red)) : (line | color(Color::Cyan)));
  }

  // One compact hint line, drawn from the active window's own keymap. It is cut
  // to the real width rather than a fixed guess, because a key that falls off the
  // end is a key nobody finds — `?` is the only other place it is written down.
  const int room = Terminal::Size().dimx - static_cast<int>(tr("app.keysHint").size()) - 4;
  std::string hints;
  if (top())
    for (const auto& [key, description] : top()->keys()) {
      const std::string entry = (hints.empty() ? " " : "   ") + key + " " + description;
      if (static_cast<int>(string_width(hints + entry)) > room) break;
      hints += entry;
    }
  footer.push_back(hbox({text(hints) | dim | flex, text(tr("app.keysHint") + " ") | dim}));

  return vbox({hbox(std::move(crumbs)), std::move(body) | flex, vbox(std::move(footer))});
}

Element App::render_help() {
  Elements rows;
  if (top())
    for (const auto& [key, description] : top()->keys())
      rows.push_back(hbox({text(key) | bold | size(WIDTH, EQUAL, 16), text(description)}));
  rows.push_back(separator());
  rows.push_back(hbox({text("Esc") | bold | size(WIDTH, EQUAL, 16), text(tr("app.help.back"))}));
  rows.push_back(hbox({text("Ctrl-C") | bold | size(WIDTH, EQUAL, 16), text(tr("app.help.quit"))}));

  const std::string title = tr("app.help.title", {{"window", top() ? top()->breadcrumb() : std::string()}});
  return window(text(" " + title + " ") | bold, vbox(std::move(rows))) |
         size(WIDTH, GREATER_THAN, 52) | clear_under | center;
}

Element App::render_modal() {
  switch (modal_) {
    case Modal::Prompt:
      return window(text(" " + modal_title_ + " ") | bold,
                    vbox({
                        prompt_input_->Render(),
                        separator(),
                        text(tr("app.prompt.footer")) | dim,
                    })) |
             size(WIDTH, GREATER_THAN, 64) | clear_under | center;

    case Modal::Picker:
    case Modal::Checklist: {
      const bool boxes = modal_ == Modal::Checklist;
      const auto shown = picker_matches();
      Elements rows;
      for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        const size_t option = static_cast<size_t>(shown[static_cast<size_t>(i)]);
        std::string box;
        if (boxes) box = tr(checked_[option] ? "app.checklist.checked" : "app.checklist.unchecked") + " ";
        auto row = text(" " + box + picker_options_[option]) | flex;
        if (i == picker_selected_) row = row | inverted | focus;
        rows.push_back(row);
      }
      if (rows.empty()) rows.push_back(ui::empty_hint(tr("app.picker.noMatch")));

      Elements content{vbox(std::move(rows)) | vscroll_indicator | yframe |
                       size(HEIGHT, LESS_THAN, 18)};
      if (picker_filter_.active || !picker_filter_.query.empty())
        content.push_back(hbox({text(" /"), text(picker_filter_.query) | bold}) | color(Color::Yellow));
      content.push_back(separator());
      content.push_back(text(tr(boxes ? "app.checklist.footer" : "app.picker.footer")) | dim);

      return window(text(" " + modal_title_ + " ") | bold, vbox(std::move(content))) |
             size(WIDTH, GREATER_THAN, 48) | clear_under | center;
    }

    case Modal::Confirm:
      return window(text(" " + tr("app.confirm.title") + " ") | bold,
                    vbox({text(" " + modal_title_), separator(),
                          text(" " + tr("app.confirm.footer")) | dim})) |
             size(WIDTH, GREATER_THAN, 40) | clear_under | center;

    case Modal::Help:
      return render_help();
    case Modal::None:
      break;
  }
  return emptyElement();
}

// ------------------------------------------------------------------ events

bool App::on_prompt_event(const Event& event) {
  if (event == Event::Escape) {
    close_modal();
    set_status(tr("app.cancelled"));
    return true;
  }
  if (event == Event::Return) {
    auto callback = std::move(on_submit_);
    const std::string value = prompt_text_;
    close_modal();
    if (callback) callback(value);
    return true;
  }
  return false;  // the Input component handles the actual typing
}

bool App::on_picker_event(const Event& event) {
  auto shown = picker_matches();
  const int count = static_cast<int>(shown.size());

  // While filtering, typing wins: `q` and space belong in the query.
  if (picker_filter_.active) {
    if (picker_filter_.on_event(event)) {
      picker_selected_ = 0;
      return true;
    }
  } else if (ui::is_back(event)) {
    close_modal();
    set_status(tr("app.cancelled"));
    return true;
  } else if (picker_filter_.on_event(event)) {
    picker_selected_ = 0;
    return true;
  }

  if (ui::motion(event, picker_selected_, count)) return true;
  if (ui::is_select(event) && count > 0) {
    auto callback = std::move(on_pick_);
    const int choice = shown[static_cast<size_t>(picker_selected_)];
    close_modal();
    if (callback) callback(choice);
    return true;
  }
  return true;  // modal: swallow everything else
}

bool App::on_checklist_event(const Event& event) {
  const auto shown = picker_matches();
  const int count = static_cast<int>(shown.size());

  // While filtering, typing wins: `q` and space belong in the query.
  if (!picker_filter_.active && ui::is_back(event)) {
    close_modal();
    set_status(tr("app.cancelled"));
    return true;
  }
  if (picker_filter_.on_event(event)) {
    picker_selected_ = 0;
    return true;
  }
  if (ui::motion(event, picker_selected_, count)) return true;

  // Space toggles rather than confirms: a checklist is several answers, so it
  // needs a separate key to say "that is all of them".
  if (event == Event::Character(' ') && count > 0) {
    const size_t option = static_cast<size_t>(shown[static_cast<size_t>(picker_selected_)]);
    checked_[option] = !checked_[option];
    return true;
  }
  if (event == Event::Return) {
    auto callback = std::move(on_check_);
    const std::vector<bool> checked = checked_;
    close_modal();
    if (callback) callback(checked);
    return true;
  }
  return true;  // modal: swallow everything else
}

bool App::on_confirm_event(const Event& event) {
  if (event == Event::Character('y') || event == Event::Character('Y')) {
    auto callback = std::move(on_yes_);
    close_modal();
    if (callback) callback();
    return true;
  }
  if (ui::is_back(event) || event == Event::Character('n')) {
    close_modal();
    set_status(tr("app.cancelled"));
    return true;
  }
  return true;
}

bool App::on_event(const Event& event) {
  // Redraw requests from the worker thread must not count as the keypress that
  // dismisses the startup notice.
  if (!notice_.empty() && event != Event::Custom) notice_.clear();

  if (event == Event::Special("\x03")) {  // Ctrl-C, before any modal can eat it
    if (screen_) screen_->ExitLoopClosure()();
    return true;
  }
  if (blocked_) return true;

  switch (modal_) {
    case Modal::Prompt: return on_prompt_event(event);
    case Modal::Picker: return on_picker_event(event);
    case Modal::Checklist: return on_checklist_event(event);
    case Modal::Confirm: return on_confirm_event(event);
    case Modal::Help:
      close_modal();
      return true;
    case Modal::None: break;
  }

  // The active window gets first refusal on every key.
  if (top() && top()->on_event(event)) return true;

  if (event == Event::Character('?')) {
    modal_ = Modal::Help;
    return true;
  }
  if (ui::is_back(event)) {
    if (stack_.size() > 1) {
      pop_window();
      focus_pending_ = true;
    } else if (screen_) {
      screen_->ExitLoopClosure()();
    }
    return true;
  }
  return false;
}
