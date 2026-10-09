// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "strings/catalogs.hpp"

// The English string table, and the reference for every other language.
//
// Shape of a key: <window or area>.<thing>[.<variant>]. A node may hold both
// its own text and children — "_" is the node's own text:
//
//   "assignee": { "_": "Assignee", "unassigned": "unassigned" }
//
// gives ticket.assignee -> "Assignee" and ticket.assignee.unassigned ->
// "unassigned". Lists are JSON arrays, read with tr_list() so their order is
// the order they are shown in.
//
// {name} placeholders are filled in by the caller. Keep every one of them in a
// translation, in whatever order the language needs.
//
// Not in here, deliberately:
//   - key chords ("j / k", "Enter / Space"): bindings, not prose. Their
//     descriptions are translated; the keys themselves are not.
//   - JQL, field names and anything else the Jira API parses.
//   - Jira's own data: status names, issue types, error bodies.
namespace translations {

const char kStringsEn[] = R"json(
{
  "app": {
    "brand": "terminal-jira",
    "working": "working…",
    "goBack": "Esc to go back",
    "keysHint": "? keys",
    "noWindow": "no window",
    "cancelled": "Cancelled.",

    "notice": { "dismiss": "any key to dismiss" },

    "help": {
      "title": "Keys · {window}",
      "back": "close this window or dialog",
      "quit": "quit immediately"
    },

    "prompt": { "footer": "Enter to save · Esc to cancel" },

    "picker": {
      "noMatch": "nothing matches",
      "footer": "Enter or Space to choose · / to filter · q or Esc to cancel"
    },

    "checklist": {
      "checked": "[x]",
      "unchecked": "[ ]",
      "footer": "Space to tick · Enter to apply · / to filter · q or Esc to cancel"
    },

    "confirm": {
      "title": "Confirm",
      "footer": "y to confirm · Esc to cancel"
    },

    "blocked": {
      "title": "Working",
      "footer": "keys are paused until this finishes · Ctrl-C quits"
    },

    "editor": {
      "cannotWrite": "Cannot write {path}",
      "failed": "Editor exited with status {code}; nothing was saved.",
      "unchanged": "No changes were made."
    },

    "browser": {
      "opened": "Opened {url}",
      "failed": "Could not launch a browser for {url}"
    }
  },

  "common": {
    "loading": "loading…",
    "nothingHere": "nothing here",
    "noValue": "—"
  },

  // Request failures, shown verbatim in the error popups. The body is Jira's
  // own text and is never translated.
  "errors": {
    "noResponse": "request failed",
    "status": "HTTP {status}",
    "statusWithBody": "HTTP {status} · {body}",
    "notJson": "non-JSON response from {path}"
  },

  "time": {
    "justNow": "just now",
    "minutes": "{count}m ago",
    "hours": "{count}h ago",
    "days": "{count}d ago"
  },

  "projects": {
    "breadcrumb": "Projects",
    "title": { "_": "Projects", "counted": "Projects ({shown}/{total})" },
    "columns": { "key": "Key", "name": "Name", "lead": "Lead" },
    "none": "no projects visible to this account",
    "noMatch": "no project matches the filter",

    "status": {
      "loading": "Loading projects…",
      "loaded": "Found {count} projects. Press Enter to open one."
    },

    "action": { "load": "Load projects" },

    "keys": {
      "move": "move down / up",
      "ends": "first / last project",
      "halfPage": "half page down / up",
      "filter": "filter by key or name",
      "open": "open the project",
      "reload": "reload the project list",
      "help": "toggle this help",
      "quit": "quit terminal-jira"
    }
  },

  "project": {
    "breadcrumb": "Projects › {key}",
    "title": "{key} — {name}",
    "tabs": ["Mine", "Open", "All", "Board"],

    "table": {
      "columns": {
        "key": "Key",
        "type": "Type",
        "status": "Status",
        "assignee": "Assignee",
        "summary": "Summary",
        "updated": "Updated"
      },
      "unassigned": "unassigned",
      "empty": "no issues here",
      "more": "more issues available — press m or scroll to the bottom"
    },

    // Both go into the JQL, so they hold across every page of a list.
    "sort": {
      "title": "Sort the lists by",
      "key": "ID",
      "summary": "Name",
      "status": "Status",
      "assignee": "Assignee",
      "parent": "Parent",
      "updated": "Last updated",
      "asc": "↑",
      "desc": "↓",
      "current": "{field} {direction}",
      "optionCurrent": "{field} {direction}  (again to reverse)",
      "hint": "t sort · {sort}"
    },

    "filter": {
      "title": "Filter the lists by",
      "status": "Status",
      "assignee": "Assignee",
      "parent": "Parent",
      "key": "ID",
      "summary": "Name",
      "any": "any",
      "option": "{field}: {value}",
      "clear": "Clear every filter",
      "is": "{field} {value}",
      "contains": "{field} ~ \"{value}\"",
      "unassigned": "Unassigned",
      "hint": "f filter",
      "active": "f filter · {filters}",
      "noMatch": "no tickets match the filter — f changes it",
      "pick": "Show tickets with {field}",
      "parentOption": "{key}  {summary}",
      "askKeys": "IDs to show, separated by commas — 12 means {project}-12; empty shows all",
      "askName": "Show tickets whose name contains — empty shows all",
      "badKey": "'{key}' is not a ticket ID.",
      "noChoices": "Jira offered nothing to filter by.",
      "loadingStatuses": "Loading statuses…",
      "loadingUsers": "Loading assignable users…",
      "loadingParents": "Loading the names of the parents…",
      "noParents": "None of the loaded tickets has a parent."
    },

    "visual": {
      "bar": "VISUAL · {count} selected · s status · a assignee · v or Esc leaves",
      "on": "Visual mode: move to select a run of tickets.",
      "off": "Left visual mode."
    },

    "board": {
      "loading": "loading boards…",
      "none": "this project has no boards",
      "emptyColumn": "empty",
      "count": "{count} issues",
      "showing": "showing {shown} of {total}",
      "offBoard": "{status} (not on the board)",
      "noStatus": "(no status)",
      "noIssues": "this board returned no issues — r reloads, b picks another board",
      "pick": { "_": "Boards in {key}", "option": "{name}  ({type})" },
      "assignee": {
        "_": "Assignee on {name}",
        "everyone": "Everyone",
        "unassigned": "Unassigned",
        "option": "{name}  ({count})",
        "hint": "f assignee",
        "active": "f · {name}",
        "none": "No cards are loaded, so there is nobody to filter by.",
        "set": "{board}: showing {name}.",
        "cleared": "{board}: showing everyone."
      },
      "columns": {
        "_": "Columns on {name}",
        "option": "{name}  ({count})",
        "hint": "c columns",
        "hidden": "c columns · {count} hidden",
        "allHidden": "At least one column has to stay visible.",
        "none": "This board has no columns to choose from yet.",
        "shown": "Showing every column.",
        "hiding": "Hiding {count} of {total} columns."
      }
    },

    "status": {
      "loadingTab": "Loading {tab} issues…",
      "loadingMore": "Loading more…",
      "tabLoaded": "{project} · {tab}: {count} issues",
      "tabLoadedPartial": "{project} · {tab}: {count} issues loaded, more available",
      "loadingBoards": "Loading boards…",
      "noBoards": "This project has no boards.",
      "loadingBoard": "Loading board {name}…",
      "boardLoaded": "Board {name}: {count} issues",
      "boardLoadedPartial": "Board {name}: {count} of {total} issues"
    },

    "action": {
      "loadTab": "Load {tab} issues in {project}",
      "loadBoards": "Load boards in {project}",
      "loadBoard": "Load board {name}",
      "loadBoardColumns": "Load the columns of board {name}",
      "loadStatuses": "Load the statuses of {project}",
      "loadUsers": "Load the assignable users of {project}",
      "loadParents": "Load the parents of tickets in {project}"
    },

    "keys": {
      "tabs": "previous / next tab",
      "jumpTab": "jump to a tab",
      "columns": "previous / next column",
      "cards": "move between cards",
      "switchBoard": "switch board",
      "pickColumns": "show / hide columns",
      "pickAssignee": "filter by assignee",
      "move": "move down / up",
      "jump": "jump / half page",
      "more": "load more issues",
      "visual": "select several tickets",
      "sort": "sort",
      "filterBy": "filter by field",
      "open": "open the ticket",
      "create": "new ticket",
      "editSummary": "edit the summary",
      "reassign": "reassign",
      "status": "change status",
      "browser": "open in browser",
      "copy": "copy a field",
      "filter": "filter",
      "reload": "reload",
      "back": "back to projects"
    }
  },

  "ticket": {
    "breadcrumb": "Projects › {key} › {issue}",
    "status": "Status",
    "type": "Type",
    "priority": "Priority",
    "assignee": { "_": "Assignee", "unassigned": "unassigned" },
    "reporter": "Reporter",
    "updated": "Updated",
    "parent": "Parent",
    "labels": "Labels",

    "description": { "_": "Description", "empty": "no description" },
    "comments": { "_": "Comments ({count})", "empty": "no comments yet" },

    "message": {
      "loading": "Loading {key}…",
      "loaded": "{key} · {status}",
      "stillLoading": "Still loading {key}…"
    },

    "action": { "load": "Load ticket {key}" },

    "copy": {
      "_": "Copy from {key}",
      "option": "{field}  {value}",
      "identifier": "Identifier",
      "name": "Name",
      "description": "Description",
      "loading": "Loading {key} to copy…",
      "load": "Load {key} to copy",
      "empty": "The {field} is empty — nothing to copy.",
      "done": "{field} copied to the clipboard.",
      "failed": {
        "linux": "No clipboard would take it. Install wl-clipboard, xclip or xsel.",
        "mac": "No clipboard would take it."
      }
    },

    "keys": {
      "fields": "move between fields",
      "edit": "change the selected field",
      "scroll": "scroll the description and comments",
      "ends": "top / bottom",
      "editSummary": "edit the summary",
      "editDescription": "edit the description in $EDITOR",
      "reassign": "reassign",
      "status": "change status",
      "comment": "add a comment in $EDITOR",
      "browser": "open in browser",
      "copy": "copy a field",
      "reload": "reload",
      "back": "back to the project"
    }
  },

  // The ticket edits, which are reachable from more than one window.
  "actions": {
    "create": {
      "loadingTypes": "Loading issue types…",
      "loadTypes": "Load issue types for {project}",
      "noTypes": "No issue types available in {project}",
      "pickType": "New issue in {project}",
      "askSummary": "Summary of the new {type}",
      "summaryRequired": "Cancelled: a summary is required.",
      "sending": "Creating issue…",
      "action": "Create {type} in {project}",
      "done": "Created {key}"
    },

    "summary": {
      "ask": "Summary of {key}",
      "unchanged": "Summary unchanged.",
      "sending": "Saving…",
      "action": "Rename {key}",
      "done": "Updated {key}"
    },

    "description": {
      "sending": "Saving description…",
      "action": "Update the description of {key}",
      "done": "Updated the description of {key}"
    },

    "assign": {
      "loadingUsers": "Loading assignable users…",
      "loadUsers": "Load assignable users for {key}",
      "pick": "Assign {key}",
      "unassignOption": "(unassigned)",
      "nobody": "nobody",
      "sending": "Assigning…",
      "action": "Assign {key} to {assignee}",
      "done": "{key} assigned to {assignee}"
    },

    "transition": {
      "loading": "Loading transitions…",
      "load": "Load the available statuses of {key}",
      "none": "No transitions available for {key}",
      "pick": { "_": "Move {key}", "option": "{name}  →  {status}" },
      "sending": "Moving {key}…",
      "action": "Move {key} to {status}",
      "done": "{key} is now {status}"
    },

    // Several tickets at once, one request each. {done} counts the tickets
    // already answered, so the first message says 0.
    "bulk": {
      "nothingChanged": "Nothing was changed.",
      "transition": {
        "loading": "Reading where {key} can move · {done} of {total} done",
        "load": "Load the available statuses of {count} tickets",
        "none": "No status is reachable from all {count} tickets.",
        "pick": "Move {count} tickets",
        "sending": "Moving {key} to {status} · {done} of {total} done",
        "action": "Move {count} tickets to {status}",
        "done": "{count} tickets are now {value}",
        "partial": "{done} of {total} tickets moved to {value}; {failed} failed"
      },
      "assign": {
        "loading": "Reading who can take {key} · {done} of {total} done",
        "load": "Load assignable users for {count} tickets",
        "pick": "Assign {count} tickets",
        "sending": "Assigning {key} to {assignee} · {done} of {total} done",
        "action": "Assign {count} tickets to {assignee}",
        "done": "{count} tickets assigned to {value}",
        "partial": "{done} of {total} tickets assigned to {value}; {failed} failed"
      }
    },

    "type": {
      "loading": "Loading issue types…",
      "load": "Load issue types for {project}",
      "none": "No issue types available in {project}",
      "pick": "Change the type of {key}",
      "sending": "Changing the type of {key}…",
      "action": "Change {key} to a {type}",
      "done": "{key} is now a {type}"
    },

    "priority": {
      "loading": "Loading priorities…",
      "load": "Load the priorities",
      "none": "This Jira has no priorities to choose from.",
      "pick": "Priority of {key}",
      "sending": "Setting the priority of {key}…",
      "action": "Set the priority of {key} to {priority}",
      "done": "{key} is now {priority} priority"
    },

    "reporter": {
      "loadingUsers": "Loading users…",
      "loadUsers": "Load the users who can report {key}",
      "none": "No users available to report {key}",
      "pick": "Reporter of {key}",
      "sending": "Changing the reporter of {key}…",
      "action": "Set the reporter of {key} to {reporter}",
      "done": "{key} is now reported by {reporter}"
    },

    "parent": {
      "loading": "Loading possible parents…",
      "load": "Load the issues in {project}",
      "pick": "Parent of {key}",
      "option": "{key}  {summary}",
      "detachOption": "(no parent)",
      "nothing": "nothing",
      "sending": "Changing the parent of {key}…",
      "action": "Set the parent of {key} to {parent}",
      "done": "{key} now hangs off {parent}"
    },

    "labels": {
      "ask": "Labels of {key}, separated by commas",
      "unchanged": "Labels unchanged.",
      "nothing": "nothing",
      "sending": "Saving labels…",
      "action": "Set the labels of {key}",
      "done": "{key} is now labelled {labels}"
    },

    "comment": {
      "sending": "Posting comment…",
      "action": "Comment on {key}",
      "done": "Commented on {key}"
    }
  },

  // Startup problems. These are printed to the terminal when the UI never
  // opens, so they say what to do rather than only what went wrong.
  "config": {
    "missing": "Missing Jira config. Set JIRA_URL and JIRA_TOKEN (and JIRA_EMAIL for Cloud), or create {path}",
    "notHttps": "JIRA_URL must start with https:// — got '{url}'",
    "plainHttp": "Refusing to send credentials over plain http to {url}. Use https://, or set TERMINAL_JIRA_ALLOW_HTTP=1 if this is a local test server.",
    "tightened": "{path} was readable by other users; its permissions are now 600.",
    "notTightened": "{path} is readable by other users and could not be tightened.",
    "unknownLanguage": "No strings for language '{code}'; using English. Available: {available}",
    "brokenLanguage": "Could not read the language file {path}: {error}"
  }
}
)json";

}  // namespace translations
