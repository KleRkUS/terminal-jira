// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "actions.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <ftxui/screen/string.hpp>

#include "translations.hpp"

namespace actions {

using translations::tr;

namespace {

// Dialog callbacks outlive the stack frame that opened them, so everything they
// need is copied in by value.
struct Target {
  std::string key;
  std::string summary;
};

// One line, short enough to sit in a dialog row next to its label.
std::string preview(const std::string& text) {
  constexpr size_t kLimit = 60;
  std::string out;
  for (char c : text) {
    if (c == '\n' || c == '\r' || c == '\t') {
      if (!out.empty() && out.back() != ' ') out += ' ';
      continue;
    }
    out += c;
  }
  if (out.size() > kLimit) out = out.substr(0, kLimit) + "…";
  return out;
}

// What came of sending one request per ticket. A ticket that fails does not
// stop the others: what was already sent cannot be taken back, and stopping
// would only leave the rest undone for a reason nobody chose.
template <typename T>
struct Batch {
  std::vector<std::pair<std::string, T>> done;              // key, Jira's answer
  std::vector<std::pair<std::string, std::string>> failed;  // key, what went wrong
};

// Runs `each(key)` for every key, one after another on the worker thread, with
// the interface blocked. `describe(key, index)` is the overlay's text while
// that ticket is in flight; it is built here, so the worker never touches the
// catalog. `finished` runs on the UI thread once the last one is back.
template <typename Each, typename Describe, typename Finished>
void run_batch(ui::Context& ctx, const Life& life, std::vector<std::string> keys, Describe describe, Each each,
               Finished finished) {
  using T = std::invoke_result_t<Each, const std::string&>;
  std::vector<std::string> messages;
  for (size_t i = 0; i < keys.size(); ++i) messages.push_back(describe(keys[i], i));

  ctx.block(messages.front());
  ctx.job_started();
  ctx.submit([&ctx, life, keys = std::move(keys), messages = std::move(messages), each = std::move(each),
              finished = std::move(finished)]() mutable {
    auto batch = std::make_shared<Batch<T>>();
    for (size_t i = 0; i < keys.size(); ++i) {
      ctx.post([&ctx, message = messages[i]] { ctx.block(message); });
      try {
        batch->done.emplace_back(keys[i], each(keys[i]));
      } catch (const JiraError& ex) {
        batch->failed.emplace_back(keys[i], ui::describe_failure(ex.status, ex.body));
      } catch (const std::exception& ex) {
        batch->failed.emplace_back(keys[i], ex.what());
      }
    }
    ctx.post([&ctx, life, batch, finished = std::move(finished)]() mutable {
      ctx.job_finished();
      ctx.unblock();
      if (!life.expired()) finished(std::move(*batch));
    });
  });
}

// One popup for the whole run rather than one per ticket, which for a long
// selection would bury the screen.
template <typename T>
void report_failures(ui::Context& ctx, const std::string& action, const Batch<T>& batch) {
  if (batch.failed.empty()) return;
  std::string detail;
  for (const auto& [key, why] : batch.failed) detail += (detail.empty() ? "" : "  ·  ") + key + ": " + why;
  ctx.report_error(action, detail);
}

std::string destination(const Transition& t) { return t.to_status.empty() ? t.name : t.to_status; }

// Reports how a change to several tickets went, and refreshes if anything
// changed. `done_key` takes {count}; `partial_key` takes {done}, {total} and
// {failed}; both take {value}, the status or assignee.
template <typename T>
void finish_batch(ui::Context& ctx, const Batch<T>& batch, const std::string& action, const char* done_key,
                  const char* partial_key, const std::string& value, const Done& on_changed) {
  const size_t total = batch.done.size() + batch.failed.size();
  report_failures(ctx, action, batch);
  if (batch.failed.empty())
    ctx.set_status(tr(done_key, {{"count", std::to_string(total)}, {"value", value}}));
  else
    ctx.set_status(tr(partial_key, {{"done", std::to_string(batch.done.size())},
                                    {"total", std::to_string(total)},
                                    {"failed", std::to_string(batch.failed.size())},
                                    {"value", value}}),
                   true);
  if (!batch.done.empty() && on_changed) on_changed();
}

// "perf, tui" -> {"perf", "tui"}. Jira rejects a label containing a space, so
// trimming is all that is needed; it says so itself if one is still invalid.
std::vector<std::string> split_labels(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  const auto flush = [&] {
    const size_t first = current.find_first_not_of(" \t");
    const size_t last = current.find_last_not_of(" \t");
    if (first != std::string::npos) out.push_back(current.substr(first, last - first + 1));
    current.clear();
  };
  for (char c : text) {
    if (c == ',')
      flush();
    else
      current += c;
  }
  flush();
  return out;
}

}  // namespace

void create_issue(ui::Context& ctx, const Life& life, const std::string& project_key,
                  std::function<void(const std::string&)> on_created) {
  ctx.set_status(tr("actions.create.loadingTypes"));
  ui::async(
      ctx, life, tr("actions.create.loadTypes", {{"project", project_key}}),
      [&ctx, project_key] { return ctx.jira().issue_types(project_key); },
      [&ctx, life, project_key, on_created = std::move(on_created)](std::vector<IssueType> types) {
        if (types.empty()) return ctx.set_status(tr("actions.create.noTypes", {{"project", project_key}}), true);
        std::vector<std::string> names;
        for (const auto& t : types) names.push_back(t.name);

        ctx.choose(tr("actions.create.pickType", {{"project", project_key}}), names,
                   [&ctx, life, project_key, types, on_created](int pick) {
          const IssueType type = types[static_cast<size_t>(pick)];
          ctx.prompt(tr("actions.create.askSummary", {{"type", type.name}}), "",
                     [&ctx, life, project_key, type, on_created](const std::string& summary) {
                       if (summary.empty()) return ctx.set_status(tr("actions.create.summaryRequired"));
                       ctx.set_status(tr("actions.create.sending"));
                       ui::async(
                           ctx, life,
                           tr("actions.create.action", {{"type", type.name}, {"project", project_key}}),
                           [&ctx, project_key, type, summary] {
                             return ctx.jira().create_issue(project_key, type.id, summary);
                           },
                           [&ctx, on_created](std::string new_key) {
                             ctx.set_status(tr("actions.create.done", {{"key", new_key}}));
                             if (on_created) on_created(new_key);
                           });
                     });
        });
      });
}

void edit_summary(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const Target target{issue.key, issue.summary};
  ctx.prompt(tr("actions.summary.ask", {{"key", target.key}}), target.summary,
             [&ctx, life, target, on_changed = std::move(on_changed)](const std::string& summary) {
               if (summary.empty() || summary == target.summary)
                 return ctx.set_status(tr("actions.summary.unchanged"));
               ctx.set_status(tr("actions.summary.sending"));
               ui::async(
                   ctx, life, tr("actions.summary.action", {{"key", target.key}}),
                   [&ctx, target, summary] {
                     ctx.jira().update_summary(target.key, summary);
                     return true;
                   },
                   [&ctx, target, on_changed](bool) {
                     ctx.set_status(tr("actions.summary.done", {{"key", target.key}}));
                     if (on_changed) on_changed();
                   });
             });
}

void edit_description(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  std::string edited;
  if (!ctx.edit_externally(issue.description, edited)) return;

  ctx.set_status(tr("actions.description.sending"));
  ui::async(
      ctx, life, tr("actions.description.action", {{"key", key}}),
      [&ctx, key, edited] {
        ctx.jira().update_description(key, edited);
        return true;
      },
      [&ctx, key, on_changed = std::move(on_changed)](bool) {
        ctx.set_status(tr("actions.description.done", {{"key", key}}));
        if (on_changed) on_changed();
      });
}

void reassign(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  ctx.set_status(tr("actions.assign.loadingUsers"));
  ui::async(
      ctx, life, tr("actions.assign.loadUsers", {{"key", key}}),
      [&ctx, key] { return ctx.jira().assignable_users(key); },
      [&ctx, life, key, on_changed = std::move(on_changed)](std::vector<User> users) {
        std::vector<std::string> names{tr("actions.assign.unassignOption")};
        for (const auto& u : users) names.push_back(u.display_name);

        ctx.choose(tr("actions.assign.pick", {{"key", key}}), names, [&ctx, life, key, users, on_changed](int pick) {
          const bool unassign = pick == 0;
          const std::string account = unassign ? "" : users[static_cast<size_t>(pick - 1)].account_id;
          const std::string label =
              unassign ? tr("actions.assign.nobody") : users[static_cast<size_t>(pick - 1)].display_name;
          ctx.set_status(tr("actions.assign.sending"));
          ui::async(
              ctx, life, tr("actions.assign.action", {{"key", key}, {"assignee", label}}),
              [&ctx, key, account] {
                ctx.jira().assign(key, account);
                return true;
              },
              [&ctx, key, label, on_changed](bool) {
                ctx.set_status(tr("actions.assign.done", {{"key", key}, {"assignee", label}}));
                if (on_changed) on_changed();
              });
        });
      });
}

void change_status(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  ctx.set_status(tr("actions.transition.loading"));
  ui::async(
      ctx, life, tr("actions.transition.load", {{"key", key}}),
      [&ctx, key] { return ctx.jira().transitions(key); },
      [&ctx, life, key, on_changed = std::move(on_changed)](std::vector<Transition> transitions) {
        if (transitions.empty()) return ctx.set_status(tr("actions.transition.none", {{"key", key}}), true);
        std::vector<std::string> names;
        for (const auto& t : transitions)
          names.push_back(t.to_status.empty() || t.to_status == t.name
                              ? t.name
                              : tr("actions.transition.pick.option", {{"name", t.name}, {"status", t.to_status}}));

        ctx.choose(tr("actions.transition.pick", {{"key", key}}), names,
                   [&ctx, life, key, transitions, on_changed](int pick) {
          const Transition chosen = transitions[static_cast<size_t>(pick)];
          const std::string status = chosen.to_status.empty() ? chosen.name : chosen.to_status;
          ctx.set_status(tr("actions.transition.sending", {{"key", key}}));
          ui::async(
              ctx, life, tr("actions.transition.action", {{"key", key}, {"status", status}}),
              [&ctx, key, chosen] {
                ctx.jira().transition(key, chosen.id);
                return true;
              },
              [&ctx, key, status, on_changed](bool) {
                ctx.set_status(tr("actions.transition.done", {{"key", key}, {"status", status}}));
                if (on_changed) on_changed();
              });
        });
      });
}

void change_status_all(ui::Context& ctx, const Life& life, std::vector<std::string> keys, Done on_changed) {
  if (keys.empty()) return;
  const std::string total = std::to_string(keys.size());
  run_batch(
      ctx, life, keys,
      [total](const std::string& key, size_t i) {
        return tr("actions.bulk.transition.loading", {{"key", key}, {"done", std::to_string(i)}, {"total", total}});
      },
      [&ctx](const std::string& key) { return ctx.jira().transitions(key); },
      [&ctx, life, total, on_changed = std::move(on_changed)](Batch<std::vector<Transition>> loaded) {
        // Without every ticket's answer there is no telling which statuses they
        // share, so a failure here stops before anything is changed.
        if (!loaded.failed.empty()) {
          report_failures(ctx, tr("actions.bulk.transition.load", {{"count", total}}), loaded);
          return ctx.set_status(tr("actions.bulk.nothingChanged"), true);
        }

        // Each ticket's workflow decides where it can go, so only the statuses
        // every one of them can reach are offered.
        std::vector<std::string> statuses;
        for (const Transition& t : loaded.done.front().second) {
          const std::string status = destination(t);
          if (std::find(statuses.begin(), statuses.end(), status) != statuses.end()) continue;
          const bool everywhere = std::all_of(loaded.done.begin(), loaded.done.end(), [&](const auto& entry) {
            return std::any_of(entry.second.begin(), entry.second.end(),
                               [&](const Transition& other) { return destination(other) == status; });
          });
          if (everywhere) statuses.push_back(status);
        }
        if (statuses.empty()) return ctx.set_status(tr("actions.bulk.transition.none", {{"count", total}}), true);

        ctx.choose(tr("actions.bulk.transition.pick", {{"count", total}}), statuses,
                   [&ctx, life, total, statuses, loaded, on_changed](int pick) {
          const std::string status = statuses[static_cast<size_t>(pick)];
          // By each ticket's own transition id: two workflows can reach the same
          // status through differently numbered transitions.
          std::vector<std::string> keys;
          std::vector<std::pair<std::string, std::string>> moves;
          for (const auto& [key, transitions] : loaded.done)
            for (const Transition& t : transitions)
              if (destination(t) == status) {
                keys.push_back(key);
                moves.emplace_back(key, t.id);
                break;
              }
          run_batch(
              ctx, life, keys,
              [total, status](const std::string& key, size_t i) {
                return tr("actions.bulk.transition.sending",
                          {{"key", key}, {"status", status}, {"done", std::to_string(i)}, {"total", total}});
              },
              [&ctx, moves](const std::string& key) {
                const auto move = std::find_if(moves.begin(), moves.end(), [&](const auto& m) { return m.first == key; });
                ctx.jira().transition(key, move->second);
                return true;
              },
              [&ctx, total, status, on_changed](Batch<bool> sent) {
                finish_batch(ctx, sent, tr("actions.bulk.transition.action", {{"count", total}, {"status", status}}),
                             "actions.bulk.transition.done", "actions.bulk.transition.partial", status, on_changed);
              });
        });
      });
}

void reassign_all(ui::Context& ctx, const Life& life, std::vector<std::string> keys, Done on_changed) {
  if (keys.empty()) return;
  const std::string total = std::to_string(keys.size());
  run_batch(
      ctx, life, keys,
      [total](const std::string& key, size_t i) {
        return tr("actions.bulk.assign.loading", {{"key", key}, {"done", std::to_string(i)}, {"total", total}});
      },
      [&ctx](const std::string& key) { return ctx.jira().assignable_users(key); },
      [&ctx, life, total, on_changed = std::move(on_changed)](Batch<std::vector<User>> loaded) {
        if (!loaded.failed.empty()) {
          report_failures(ctx, tr("actions.bulk.assign.load", {{"count", total}}), loaded);
          return ctx.set_status(tr("actions.bulk.nothingChanged"), true);
        }

        // Who may hold a ticket can differ between tickets, so only the people
        // Jira offers for every one of them are listed.
        std::vector<User> users;
        for (const User& user : loaded.done.front().second) {
          const bool everywhere = std::all_of(loaded.done.begin(), loaded.done.end(), [&](const auto& entry) {
            return std::any_of(entry.second.begin(), entry.second.end(),
                               [&](const User& other) { return other.account_id == user.account_id; });
          });
          if (everywhere) users.push_back(user);
        }

        std::vector<std::string> names{tr("actions.assign.unassignOption")};
        for (const User& u : users) names.push_back(u.display_name);
        std::vector<std::string> keys;
        for (const auto& entry : loaded.done) keys.push_back(entry.first);

        ctx.choose(tr("actions.bulk.assign.pick", {{"count", total}}), names,
                   [&ctx, life, total, users, keys, on_changed](int pick) {
          const bool unassign = pick == 0;
          const std::string account = unassign ? "" : users[static_cast<size_t>(pick - 1)].account_id;
          const std::string label =
              unassign ? tr("actions.assign.nobody") : users[static_cast<size_t>(pick - 1)].display_name;
          run_batch(
              ctx, life, keys,
              [total, label](const std::string& key, size_t i) {
                return tr("actions.bulk.assign.sending",
                          {{"key", key}, {"assignee", label}, {"done", std::to_string(i)}, {"total", total}});
              },
              [&ctx, account](const std::string& key) {
                ctx.jira().assign(key, account);
                return true;
              },
              [&ctx, total, label, on_changed](Batch<bool> sent) {
                finish_batch(ctx, sent, tr("actions.bulk.assign.action", {{"count", total}, {"assignee", label}}),
                             "actions.bulk.assign.done", "actions.bulk.assign.partial", label, on_changed);
              });
        });
      });
}

void change_type(ui::Context& ctx, const Life& life, const std::string& project_key, const Issue& issue,
                 Done on_changed) {
  const std::string key = issue.key;
  ctx.set_status(tr("actions.type.loading"));
  ui::async(
      ctx, life, tr("actions.type.load", {{"project", project_key}}),
      [&ctx, project_key] { return ctx.jira().issue_types(project_key); },
      [&ctx, life, key, project_key, on_changed = std::move(on_changed)](std::vector<IssueType> types) {
        if (types.empty()) return ctx.set_status(tr("actions.type.none", {{"project", project_key}}), true);
        std::vector<std::string> names;
        for (const auto& t : types) names.push_back(t.name);

        ctx.choose(tr("actions.type.pick", {{"key", key}}), names,
                   [&ctx, life, key, types, on_changed](int pick) {
          const IssueType chosen = types[static_cast<size_t>(pick)];
          ctx.set_status(tr("actions.type.sending", {{"key", key}}));
          ui::async(
              ctx, life, tr("actions.type.action", {{"key", key}, {"type", chosen.name}}),
              [&ctx, key, chosen] {
                ctx.jira().update_type(key, chosen.id);
                return true;
              },
              [&ctx, key, chosen, on_changed](bool) {
                ctx.set_status(tr("actions.type.done", {{"key", key}, {"type", chosen.name}}));
                if (on_changed) on_changed();
              });
        });
      });
}

void change_priority(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  ctx.set_status(tr("actions.priority.loading"));
  ui::async(
      ctx, life, tr("actions.priority.load"), [&ctx] { return ctx.jira().priorities(); },
      [&ctx, life, key, on_changed = std::move(on_changed)](std::vector<Priority> priorities) {
        if (priorities.empty()) return ctx.set_status(tr("actions.priority.none"), true);
        std::vector<std::string> names;
        for (const auto& p : priorities) names.push_back(p.name);

        ctx.choose(tr("actions.priority.pick", {{"key", key}}), names,
                   [&ctx, life, key, priorities, on_changed](int pick) {
          const Priority chosen = priorities[static_cast<size_t>(pick)];
          ctx.set_status(tr("actions.priority.sending", {{"key", key}}));
          ui::async(
              ctx, life, tr("actions.priority.action", {{"key", key}, {"priority", chosen.name}}),
              [&ctx, key, chosen] {
                ctx.jira().update_priority(key, chosen.id);
                return true;
              },
              [&ctx, key, chosen, on_changed](bool) {
                ctx.set_status(tr("actions.priority.done", {{"key", key}, {"priority", chosen.name}}));
                if (on_changed) on_changed();
              });
        });
      });
}

void change_reporter(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  ctx.set_status(tr("actions.reporter.loadingUsers"));
  // The same user list as for the assignee: Jira has no separate "can report"
  // search, and it rejects anyone who does not belong here.
  ui::async(
      ctx, life, tr("actions.reporter.loadUsers", {{"key", key}}),
      [&ctx, key] { return ctx.jira().assignable_users(key); },
      [&ctx, life, key, on_changed = std::move(on_changed)](std::vector<User> users) {
        if (users.empty()) return ctx.set_status(tr("actions.reporter.none", {{"key", key}}), true);
        std::vector<std::string> names;
        for (const auto& u : users) names.push_back(u.display_name);

        ctx.choose(tr("actions.reporter.pick", {{"key", key}}), names,
                   [&ctx, life, key, users, on_changed](int pick) {
          const User chosen = users[static_cast<size_t>(pick)];
          ctx.set_status(tr("actions.reporter.sending", {{"key", key}}));
          ui::async(
              ctx, life, tr("actions.reporter.action", {{"key", key}, {"reporter", chosen.display_name}}),
              [&ctx, key, chosen] {
                ctx.jira().update_reporter(key, chosen.account_id);
                return true;
              },
              [&ctx, key, chosen, on_changed](bool) {
                ctx.set_status(
                    tr("actions.reporter.done", {{"key", key}, {"reporter", chosen.display_name}}));
                if (on_changed) on_changed();
              });
        });
      });
}

void change_parent(ui::Context& ctx, const Life& life, const std::string& project_key, const Issue& issue,
                   Done on_changed) {
  const std::string key = issue.key;
  ctx.set_status(tr("actions.parent.loading"));
  ui::async(
      ctx, life, tr("actions.parent.load", {{"project", project_key}}),
      [&ctx, project_key, key] { return ctx.jira().parent_candidates(project_key, key); },
      [&ctx, life, key, on_changed = std::move(on_changed)](std::vector<Issue> candidates) {
        // First entry detaches, so there is a way back out of a wrong parent.
        std::vector<std::string> names{tr("actions.parent.detachOption")};
        for (const auto& c : candidates)
          names.push_back(tr("actions.parent.option", {{"key", c.key}, {"summary", c.summary}}));

        ctx.choose(tr("actions.parent.pick", {{"key", key}}), names,
                   [&ctx, life, key, candidates, on_changed](int pick) {
          const bool detach = pick == 0;
          const std::string parent = detach ? "" : candidates[static_cast<size_t>(pick - 1)].key;
          const std::string label = detach ? tr("actions.parent.nothing") : parent;
          ctx.set_status(tr("actions.parent.sending", {{"key", key}}));
          ui::async(
              ctx, life, tr("actions.parent.action", {{"key", key}, {"parent", label}}),
              [&ctx, key, parent] {
                ctx.jira().update_parent(key, parent);
                return true;
              },
              [&ctx, key, label, on_changed](bool) {
                ctx.set_status(tr("actions.parent.done", {{"key", key}, {"parent", label}}));
                if (on_changed) on_changed();
              });
        });
      });
}

void edit_labels(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  std::string current;
  for (const auto& label : issue.labels) current += (current.empty() ? "" : ", ") + label;

  // A picker chooses one thing; labels are a set, so this one is text.
  ctx.prompt(tr("actions.labels.ask", {{"key", key}}), current,
             [&ctx, life, key, current, on_changed = std::move(on_changed)](const std::string& edited) {
               if (edited == current) return ctx.set_status(tr("actions.labels.unchanged"));
               const std::vector<std::string> labels = split_labels(edited);
               std::string shown;
               for (const auto& l : labels) shown += (shown.empty() ? "" : ", ") + l;
               if (shown.empty()) shown = tr("actions.labels.nothing");

               ctx.set_status(tr("actions.labels.sending", {{"key", key}}));
               ui::async(
                   ctx, life, tr("actions.labels.action", {{"key", key}}),
                   [&ctx, key, labels] {
                     ctx.jira().update_labels(key, labels);
                     return true;
                   },
                   [&ctx, key, shown, on_changed](bool) {
                     ctx.set_status(tr("actions.labels.done", {{"key", key}, {"labels", shown}}));
                     if (on_changed) on_changed();
                   });
             });
}

void add_comment(ui::Context& ctx, const Life& life, const Issue& issue, Done on_changed) {
  const std::string key = issue.key;
  std::string body;
  if (!ctx.edit_externally("", body)) return;

  ctx.set_status(tr("actions.comment.sending"));
  ui::async(
      ctx, life, tr("actions.comment.action", {{"key", key}}),
      [&ctx, key, body] {
        ctx.jira().add_comment(key, body);
        return true;
      },
      [&ctx, key, on_changed = std::move(on_changed)](bool) {
        ctx.set_status(tr("actions.comment.done", {{"key", key}}));
        if (on_changed) on_changed();
      });
}

void copy_detail(ui::Context& ctx, const Issue& issue) {
  struct Part {
    std::string label;
    std::string value;
  };
  const std::vector<Part> parts{
      {tr("ticket.copy.identifier"), issue.key},
      {tr("ticket.copy.name"), issue.summary},
      {tr("ticket.copy.description"), issue.description},
  };

  // The labels are padded to the widest of them so the values line up, the way
  // they do in the field list this dialog is opened from. Measured in columns,
  // not bytes: a translated label is not all ASCII.
  int width = 0;
  for (const Part& part : parts) width = std::max(width, ftxui::string_width(part.label));

  std::vector<std::string> options;
  for (const Part& part : parts) {
    const std::string padded =
        part.label + std::string(static_cast<size_t>(width - ftxui::string_width(part.label)), ' ');
    options.push_back(tr("ticket.copy.option", {{"field", padded}, {"value", preview(part.value)}}));
  }

  ctx.choose(tr("ticket.copy", {{"key", issue.key}}), options, [&ctx, parts](int pick) {
    const Part& part = parts[static_cast<size_t>(pick)];
    if (part.value.empty()) return ctx.set_status(tr("ticket.copy.empty", {{"field", part.label}}), true);
    if (ctx.copy_to_clipboard(part.value))
      ctx.set_status(tr("ticket.copy.done", {{"field", part.label}}));
    else
      ctx.set_status(tr(std::string("ticket.copy.failed.") + TERMINAL_JIRA_PLATFORM), true);
  });
}

void copy_detail(ui::Context& ctx, const Life& life, const std::string& issue_key) {
  ctx.set_status(tr("ticket.copy.loading", {{"key", issue_key}}));
  ui::async(
      ctx, life, tr("ticket.copy.load", {{"key", issue_key}}),
      [&ctx, issue_key] { return ctx.jira().issue_detail(issue_key); },
      [&ctx](Issue issue) { copy_detail(ctx, issue); });
}

void open_in_browser(ui::Context& ctx, const Issue& issue) {
  if (issue.key.empty()) return;
  ctx.open_in_browser(ctx.jira().base_url() + "/browse/" + issue.key);
}

}  // namespace actions
