# Architecture

## Windows, not panes

lazygit shows several panes at once. terminal-jira shows exactly one **window** at a
time: a window owns the whole screen, its own layout, and its own keymap. The
alternative — one screen with focusable panes — was rejected because the meaning
of `j` would then depend on invisible focus state.

There are three, in `src/windows/`:

| Window | File | Shows |
| --- | --- | --- |
| Projects | `projects_window.cpp` | every project you can browse |
| Project | `project_window.cpp` | tabs of issue tables, plus the board |
| Ticket | `ticket_window.cpp` | one issue: an editable field list, the description and the comments |

They form a stack. `App` keeps `std::vector<std::unique_ptr<ui::Window>>`, renders
only `back()`, and gives it every key first. Whatever the window does not handle
falls through to the application, which is where the universal keys live. That
ordering is what lets `q` mean "back" everywhere without each window repeating it,
and lets a window override it when it has to.

Every window implements four things:

```cpp
std::string breadcrumb() const;              // "Projects › ENG › ENG-1"
ftxui::Element render();
bool on_event(const ftxui::Event&);          // false = not mine, pass it up
std::vector<ui::KeyHelp> keys() const;       // drives the footer and ? overlay
```

`keys()` is the single source of truth for a window's keymap. The footer hint line
and the `?` overlay are both generated from it, so a binding that is not listed
there is invisible to the user.

`on_focus()` is called when a window becomes visible, including on return from a
child. That is where loading belongs. The Projects window loads once; the Project
window refreshes quietly on return, so a status you changed on a ticket is already
correct in the table behind it.

### Adding a window

1. Subclass `ui::Window` in `src/windows/`.
2. Take `ui::Context&` plus whatever it displays, by value.
3. Load in `on_focus()` via `ui::async`, never in the constructor — the first
   frame has not been drawn yet and results cannot be posted before the loop
   starts.
4. Push it with `ctx_.push_window(std::make_unique<YourWindow>(...))`.
5. Add the source file to `CMakeLists.txt`.

## The Context interface

Windows never touch the screen, the worker thread, or each other. They get
`ui::Context` (implemented by `App`), which offers the client, `submit`/`post`,
the status bar, the error popups, the dialogs, `$EDITOR`, the browser, and
stack manipulation.

This exists so a window cannot reach into application internals, and so the
dialogs look the same no matter who opened them. Add to `Context` only when more
than one window needs the capability; otherwise keep it in the window.

## Threading

Two threads, with one rule: **the UI thread never blocks.**

```
UI thread (FTXUI loop)          worker thread
  key -> window -> action
    ui::async(action, work, done) -> work()       calls JiraClient
                                  |               (serialized: one curl handle)
    done(result) <-------------- post(closure)
    + Event::Custom to redraw
```

`App::submit` pushes onto a queue drained by a single worker. One worker, not a
pool, because `JiraClient` owns one curl handle and because serialising requests
is also politer to Jira's rate limiter. `App::post` hands a closure back to the
loop via `ScreenInteractive::Post`.

"Never blocks" is about the thread, not the keyboard. A change sent ticket by
ticket (visual mode's `s` and `a`) calls `Context::block`, which keeps drawing
but swallows every key except Ctrl-C and shows a progress window until
`unblock`. The loop is one worker job that posts a progress message per ticket;
a moved cursor or a closed window halfway through would leave the user unsure
which tickets were changed. See `run_batch` in `actions.cpp`.

`ui::async` wraps the pattern and handles the parts that are easy to get wrong:

- a `JiraError` from `work()` becomes an error popup headed by the `action`
  label, and `done` is skipped;
- an optional `on_error` runs on the UI thread, for resetting `loading` flags;
- the window's life token is checked before `done` runs, and held weakly so a
  pending callback cannot keep a dead window alive.

So a load looks like this, and nothing else needs to know about threads:

```cpp
ui::async(
    ctx_, life, "Load projects", [this] { return ctx_.jira().projects(); },
    [this](std::vector<Project> projects) { projects_ = std::move(projects); });
```

### Three FTXUI behaviours worth knowing

**Posted closures do not invalidate the frame.** `HandleTask` sets
`frame_valid_ = false` for events only, and `Draw` returns early when the frame is
valid. A result delivered by `post` alone therefore updates state that is never
drawn — the symptom is a view stuck on "loading…" until you press an unrelated
key. `App::post` posts `Event::Custom` after every closure to force the redraw.

**`hbox` grows children from what they ask for, not evenly.** Extra width is
handed out in proportion to each child's own requested size, so a board column
holding a long summary ends up wider than its neighbours. `ui::equal_columns` is a
small layout node that asks for nothing and splits the width it is given instead;
children that cannot grow, like the separators between columns, keep their own
width and are not counted.

**`Post` before the loop starts is dropped.** `task_sender_` is created in
`Install()`, which runs inside `Loop()`. So the first window is queued with
`push_window` and its `on_focus()` fires during the first render, by which time
posting works.

## Dialogs

`App` hosts all five modal kinds — prompt, picker, checklist, confirm, help — and
intercepts events before the window sees them. A dialog callback may outlive the window that
opened it, so `actions` takes `std::weak_ptr<int>` and windows wrap their own
callbacks in `ui::guarded`.

The picker supports `/` filtering with the same `ui::Filter` the lists use, which
is why filter handling and key dispatch look identical in both places. The
checklist is the picker with boxes: same options, same cursor, same filter, but
`Space` toggles instead of confirming and `Enter` returns every flag at once,
because it answers several questions rather than one.

## Talking to the system

`$EDITOR`, the browser, the clipboard, the config directory and timestamp parsing
go through `platform.hpp`. The windows call `ui::Context`, and `ui::Context` calls
`platform`; no window runs a subprocess itself. Which file is compiled is a build
attribute, `-DTERMINAL_JIRA_PLATFORM`, defaulting to `linux`. The host machine is
not detected. `mac` selects `platform/mac.cpp`. `windows` selects
`platform/windows.cpp`, which is not written yet, so that choice fails
configuration until it is. The same attribute is compiled into the target as
the `TERMINAL_JIRA_PLATFORM` macro.

Child processes see a scrubbed environment: `JIRA_TOKEN` and `JIRA_EMAIL` are
unset for as long as one is running, because an editor or a clipboard helper has
no business reading the credentials.

On Linux the clipboard tries `wl-copy`, then `xclip`, then `xsel`. A missing
helper makes the shell exit 127, which reads as "try the next", and the last
resort is OSC 52, which asks the terminal to hold the text and is what works over
ssh. It has no reply, so a terminal that ignores the escape leaves the app
reporting a success it cannot verify. The macOS module uses `pbcopy` and then the
same escape, opens urls with `open`, and keeps the config under
`~/Library/Application Support/terminal-jira`.

## Status bar and notices

Two separate things, because they behave differently:

- **Status** (`set_status`) is transient: the last thing that happened, red on
  error. Each message replaces the previous one.
- **Notice** (`set_notice`) is sticky until the first keypress. Used for startup
  warnings that must not be scrolled away by the initial load — the config
  permission warning is one, and it was invisible until it stopped being a status
  message.

Actions that trigger a refresh pass `announce = false` to the reload so the
confirmation of what the user just did survives on screen.

## Strings

Nothing the user reads is written in the code. `tr("ticket.assignee")` looks the
text up in a catalog, and the catalogs are the only files that contain English:

```
src/translations.{hpp,cpp}  tr(), tr_list(), use_language()
src/strings/en.cpp          the English catalog, as JSON in a raw string literal
```

There is no translation library behind this; it is a map from dotted keys to
text, which is all the app needs.

The catalog is a tree so that related texts sit together while it is being
edited, and lookups use the dotted path. A node can carry its own text and
children at the same time — `"_"` is the node's own text:

```jsonc
"assignee": { "_": "Assignee", "unassigned": "unassigned" }
```

gives both `ticket.assignee` and `ticket.assignee.unassigned`. At load the tree
is flattened once into a `key -> text` map, so a lookup is a hash and not a walk.
Ordered lists (the tab names) are JSON arrays, flattened to `project.tabs.0`
upwards and read back in order by `tr_list`.

Sentences are composed by the catalog, not by the code: `tr` fills `{name}`
placeholders, so a translation can put them wherever its grammar needs them. An
unknown key renders as `!the.key!` and an unfilled placeholder stays as `{name}`
— both are mistakes in the code, so they are made visible rather than quietly
left blank.

Catalogs are embedded in the binary rather than installed as data files, so
there is no path to get wrong at runtime. A language is added by copying
`en.cpp`, listing it in `kBuiltins`, and adding it to `CMakeLists.txt`; a user
can also drop `~/.config/terminal-jira/locales/<code>.json` in without rebuilding.
Either way English is loaded first and stays underneath as the fallback, so a
partial or outdated translation shows English for what it misses instead of
`!key!`.

`use_language` is called from `load_config`, before anything is drawn, so even
the startup errors come out translated. The catalog is read from both threads
(`describe_failure` runs on the worker) and is therefore behind a shared mutex,
which also leaves room for switching language while the app is running.

What is deliberately *not* in the catalog: key chords, JQL and field names, and
Jira's own data — status names, issue types and error bodies are passed through
as they arrive.

## Error popups

A failed request becomes a popup in the bottom right corner rather than a status
line, because a request can fail while the user is reading something else, and
because several can fail at once.

`report_error(action, detail)` appends to `toasts_`. Newest goes last, which is
also lowest on screen, so a stack reads top to bottom in the order things broke.
Five are kept; older ones are dropped. Each lives four seconds, fading over the
last three and a half, and the whole stack is drawn with `dbox` over the body so
it never covers the chrome.

The detail text comes from `ui::describe_failure`, which formats `HTTP 403 ·
{body}` with whitespace collapsed so a pretty-printed JSON body or an HTML error
page still fits on a few lines. The body is shown as Jira sent it. Translating it
into friendlier prose would mean guessing, and the real response is what someone
needs in order to understand a permissions or validation failure.

Expiry is driven by a ticker thread posting `Event::Custom` every 100ms while any
popup is visible, since nothing else would redraw the screen if the user has
stopped typing. It idles while the stack is empty, and is joined before the
screen is torn down.
