# Working on terminal-jira

A terminal UI for Jira in C++20: FTXUI for rendering, libcurl for HTTP,
nlohmann/json for parsing. Around 2,600 lines in `src/`. No framework magic —
read the file you are changing and the one above it.

Start here, then follow the links when you need depth:

| Document | What it covers |
| --- | --- |
| [docs/architecture.md](docs/architecture.md) | windows, the context interface, threading, event flow |
| [docs/jira-api.md](docs/jira-api.md) | which endpoints are used and the traps in each |
| [docs/testing.md](docs/testing.md) | how to verify a change without a Jira account |
| [CONTRIBUTING.md](CONTRIBUTING.md) | dependencies, build, tooling |

## The shape of the code

```
src/
  main.cpp            loads config, constructs the client and app
  config.{hpp,cpp}    config file + env vars, credential hygiene
  translations.*      tr(): dotted-key lookup into the string catalog
  strings/            the catalogs themselves, one file per language
  jira_client.*       every HTTP call; the only file that knows about curl
  adf.*               Atlassian Document Format <-> plain text
  models.hpp          plain structs: Project, Board, Issue, Comment, ...
  ui.{hpp,cpp}        Window base class, Context interface, async(), render helpers
  app.{hpp,cpp}       window stack, worker thread, dialogs, status bar, popups
  actions.{hpp,cpp}   ticket mutations shared between windows
  windows/            one file per full-screen view
```

Layering runs one way: `windows/` and `actions` use `ui` and `models`; `app`
implements `ui::Context`; `jira_client` knows nothing about the UI. A window must
never include `app.hpp`, and `jira_client` must never include anything from the
UI.

## Rules that are easy to break

These are not style preferences. Each one has already caused a bug.

**Never call `JiraClient` from the UI thread.** It owns a single curl handle and
is not thread-safe. Every call goes through `ui::async`, which runs the work on
the one worker thread and delivers the result back on the UI thread. A direct
call also freezes the interface for the length of the request.

**Anything posted from the worker thread must also request a redraw.** FTXUI
marks the frame dirty for input events but not for posted closures, so a result
that arrives without an accompanying event updates state that is never drawn.
`App::post` posts `Event::Custom` alongside every closure. Do not remove it.

**Hold window life tokens weakly.** A dialog or a request can finish after its
window is gone. `ui::async` and `ui::guarded` take `std::weak_ptr<int>`; if you
capture the `shared_ptr` instead, the token never expires and the callback writes
to a destroyed window. Pass `life` and let it convert.

**Check text input before universal keys.** `q` closes a window and space
confirms a choice, but while a `/` filter or a prompt is open both are ordinary
characters. Give the filter the event first, then `ui::is_back` and
`ui::is_select`. Getting this backwards makes it impossible to search for
`queue`.

**No user-visible string literals outside `src/strings/`.** Every label, title,
hint, status message and popup action name comes from `tr("some.key")`. A
literal in a window compiles and works, and is then invisible to translation —
which is exactly the bug the catalog exists to prevent. Jira's own data (status
names, issue types, error bodies) and key chords are not strings to translate.

**Mutating the window stack is deferred.** `push_window` and `pop_window` queue
the change; `App::apply_pending` applies it at the start of the next render, so a
window is never destroyed while it is handling its own event.

## Conventions

Comments explain a constraint the code cannot state — a protocol quirk, a
threading rule, a reason an obvious simpler version fails. They do not narrate
what the next line does.

Names say what a thing is for in the domain: `board_columns`, `assignable_users`,
`quiet refresh`. Failures travel as `JiraError` from `JiraClient`, carrying the
status and raw body; `ui::async` turns them into an error popup labelled with the
`action` string you passed it. The UI layer itself does not throw.

Every `ui::async` call takes a human-readable action name — "Move ENG-12 to
Done", not "transition". It is what the user reads when the request fails, so
phrase it from their point of view.

Jira is the only authority on permissions. Never gate an action on a guess about
what the user may do — ask Jira for the transitions or assignable users and
render what comes back.

## Before you call a change done

1. It compiles with `-Wall -Wextra` and no new warnings.
2. `tests/smoke.sh` passes — it drives the real binary through all three windows
   and asserts on both the rendered frames and the requests sent.
3. If you touched `adf.*` or the string catalog, `adf_test` and `translations_test` pass
   too.
4. If you changed a keybinding, the window's `keys()` reflects it, since the
   footer and the `?` overlay are generated from it.

A change that cannot be exercised by the harness in `docs/testing.md` needs a
note in the final summary saying what remains unverified.
