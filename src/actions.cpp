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
      ctx.set_status(tr("ticket.copy.failed"), true);
  });
}

void open_in_browser(ui::Context& ctx, const Issue& issue) {
  if (issue.key.empty()) return;
  ctx.open_in_browser(ctx.jira().base_url() + "/browse/" + issue.key);
}

}  // namespace actions
