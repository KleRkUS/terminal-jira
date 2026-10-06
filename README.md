# terminal-jira

A keyboard-driven Jira client for the terminal: vim motions, no mouse, and
nothing on screen you did not ask for.

With thanks to [LazyVim](https://github.com/LazyVim/LazyVim) and
[lazygit](https://github.com/jesseduffield/lazygit), which showed what a
keyboard-first tool should feel like — everything one or two keys away, the
current context always visible, and no configuration needed before the first
useful thing happens. The name is a placeholder; the debt is not.

It covers the day-to-day loop of working on tickets — switch project, see what is
assigned to you, read a ticket, retitle it, reassign it, move it along, glance at
the board — without opening a browser tab.

```
 terminal-jira  Projects › ENG                                                               Esc to go back
╭ ENG — Engineering ───────────────────────────────────────────────────────────────────────────────────────╮
│ Mine · Open · All · Board                                                                    Engineering │
├──────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Key          Type      Status          Assignee              Summary                           Updated   │
├──────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ ENG-1        Task      In Progress     AL Ada Lovelace       Make widget 1 faster than before 9h ago    ┃│
│ ENG-2        Bug       Done            AL Ada Lovelace       Make widget 2 faster than before 9h ago    ┃│
│ ENG-3        Task      To Do           AL Ada Lovelace       Make widget 3 faster than before 9h ago    ╹│
│ ENG-4        Bug       In Progress     ·· unassigned         Make widget 4 faster than before 9h ago     │
╰──────────────────────────────────────────────────────────────────────────────────────────────────────────╯
 ENG · Mine: 8 issues
 [ / ] previous / next tab   1 … 4 jump to Mine / Open / All / Board   j / k move down / up   g / G, ? keys
```

## Install

You need a C++20 compiler, CMake 3.20+, and libcurl. FTXUI and nlohmann/json are
downloaded automatically during configuration, so you do not install those.

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake libcurl4-openssl-dev

# Fedora
sudo dnf install gcc-c++ cmake libcurl-devel

# macOS
brew install cmake curl
```

Then build:

```bash
git clone <this-repo> terminal-jira && cd terminal-jira
cmake -S . -B build
cmake --build build -j
```

The binary is `build/terminal-jira`. Copy it onto your `PATH` if you like:

```bash
mkdir -p ~/.local/bin && cp build/terminal-jira ~/.local/bin/
```

## Connect it to your Jira

Create `~/.config/terminal-jira/config.json`:

```json
{
  "url": "https://your-org.atlassian.net",
  "email": "you@example.com",
  "token": "YOUR_API_TOKEN"
}
```

**Jira Cloud** — `token` is an API token from
[id.atlassian.com → Security → API tokens](https://id.atlassian.com/manage-profile/security/api-tokens),
and `email` is the account it belongs to.

**Jira Server or Data Center** — leave `email` empty and put a personal access
token in `token` (Profile → Personal Access Tokens). It is then sent as a bearer
token.

Run `terminal-jira`. If something is wrong with the configuration it tells you before
the interface opens.

You can override any field with an environment variable, which is handy if you
work across two instances:

```bash
JIRA_URL=https://other.atlassian.net terminal-jira
```

| Variable | Overrides |
| --- | --- |
| `JIRA_URL` | `url` |
| `JIRA_EMAIL` | `email` |
| `JIRA_TOKEN` | `token` |
| `TERMINAL_JIRA_LANG` | `language` |
| `TERMINAL_JIRA_ALLOW_HTTP` | set to `1` to permit a plain `http://` URL |

### Language

Every word the interface shows comes from a string catalog, and English is the
only one that ships today. If you want another, write one: copy
[src/strings/en.cpp](src/strings/en.cpp) into
`~/.config/terminal-jira/locales/<code>.json` — the part between the `R"json(` and
`)json` markers is the file — translate the values, and point the config at it:

```json
{ "language": "de" }
```

Translate as much or as little as you like; anything you leave out falls back to
English. Keep the `{placeholders}` in each line, in whatever order your language
needs them. Jira's own words — status names, issue types, the text in error
popups — come from your Jira and are shown as they arrive.

Your token is the only secret involved. The config file is chmodded to `600` on
startup if anyone else can read it, a plain `http://` URL is refused unless you
opt in, and your editor and browser are launched without the token in their
environment. See [CONTRIBUTING.md](CONTRIBUTING.md) for the details.

## Using it

Three keys mean the same thing everywhere:

| Key | Does |
| --- | --- |
| `Enter` or `Space` | choose what is highlighted — a project, a ticket, an assignee, a status |
| `q` or `Esc` | go back one level; from the project list, quit |
| `?` | the keys for wherever you are |

`j`/`k` move, `g`/`G` jump to the ends, `d`/`u` move by half a page, `/` filters
the current list. Arrow keys and page keys work too.

While you are typing — in a `/` filter or a text prompt — `q` and `Space` are just
characters, so you can search for `queue` or write a summary with spaces in it.
`Esc` leaves the filter or cancels the prompt.

### Picking a project

The first screen lists every project you can browse. Filter with `/`, open with
`Enter`. `r` reloads.

### Working in a project

Four tabs, switched with `[` and `]` or jumped to with `1`–`4`:

| Tab | Shows |
| --- | --- |
| **Mine** | your unfinished issues — where you will usually start |
| **Open** | everything in the project that is not done |
| **All** | the full history, most recently updated first |
| **Board** | the project's agile board, as columns of cards |

The first three are tables that load more as you scroll; `m` fetches the next page
immediately. The Board tab shows the real columns from your board's configuration,
one card per issue with its key, summary, type, priority, assignee initials and
age; the columns always share the width equally, however long the summaries in
them are. `h` and `l` move between columns, `b` switches to another board, and `c`
opens a checklist of the columns — `Space` ticks one off, `Enter` applies, and the
heading then says how many are hidden. `f` filters the cards to one assignee: the
list is the people actually on the board, plus *Unassigned* and *Everyone*.

From any tab you can work on the highlighted issue without opening it:

| Key | Does |
| --- | --- |
| `n` | create a ticket — pick a type, type a summary |
| `e` | change the summary |
| `a` | reassign, or unassign |
| `s` | move it to another status |
| `o` | open it in your browser |
| `y` | copy its identifier, name or description |
| `r` | reload |

Only the statuses your workflow actually allows are offered, and only people who
can be assigned to that issue appear in the list.

### Reading a ticket

`Enter` opens the highlighted issue: its fields, the full description, and the
comments.

The fields at the top are a list you walk with `j`/`k`, and every one of them can
be changed from here — `Enter` or `Space` on the highlighted field opens a popup
with the choices, fetched from your Jira as you ask for them:

| Field | You get |
| --- | --- |
| Status | the transitions Jira allows from where the ticket is now |
| Type | the issue types of the project |
| Priority | the priorities this Jira defines |
| Assignee | the users who can be assigned, plus *(unassigned)* |
| Reporter | the same list of users |
| Parent | the other issues in the project, plus *(no parent)* |
| Labels | a text box, since labels are a set — comma separated |

`q` or `Esc` closes a popup without changing anything. Inside one, `j`/`k` move
and `/` filters, which matters once a list is long.

Because the fields own `j`/`k`, the description and comments scroll with `J`/`K`,
and `g`/`G` jump to the top and bottom of them.

Everything from the project window works here too, plus two that need more room
than one line:

| Key | Does |
| --- | --- |
| `E` | edit the description in `$EDITOR` |
| `c` | write a comment in `$EDITOR` |

Both open `$VISUAL`, `$EDITOR`, or `vi`, and save when you exit the editor. Leave
the text unchanged and nothing is sent.

`y` copies something out of the ticket: it lists the identifier, the name and the
description, and the one you choose goes to your clipboard. The same key works on
the highlighted row of a list and on the highlighted card of a board; those views
do not carry the description, so it is loaded before the list appears. On Linux
this needs `wl-copy`, `xclip` or `xsel`; the macOS build uses `pbcopy`. Over ssh
either build asks the terminal itself to hold the text, which most terminals support.

Jira decides what you may change. If your account cannot set the reporter, the
popup still offers it and Jira's refusal shows up in the corner — the app never
guesses at your permissions.

## Good to know

Requests never block the interface. A `working…` marker appears in the header
while something is in flight. If a request fails, a small popup appears in the
bottom right corner naming what you were doing and showing exactly what Jira
replied — the status code and the response body, not a paraphrase. Popups stack
if several things fail, fade, and disappear after four seconds. Nothing is
modal, so a failure never interrupts what you are doing.

Lists refresh themselves when you return to them, so a status you changed on a
ticket is already correct in the table behind it.

Descriptions and comments are translated between Jira's rich-text format and plain
text. Formatting with no plain-text equivalent — panels, embedded media, nested
tables — is flattened when displayed, so editing a heavily formatted description
in your editor will simplify it. Reading is always safe; it is saving that
rewrites.

## Troubleshooting

**"Refusing to send credentials over plain http"** — your URL starts with
`http://`. Use `https://`, or set `TERMINAL_JIRA_ALLOW_HTTP=1` if it really is a local
test server.

**"Missing Jira config"** — neither the config file nor the environment variables
supplied a URL and token. The message includes the path it looked at.

**`Jira 401`** — the token or email is wrong. Cloud needs *both* the account email
and an API token; a Cloud token alone will not authenticate.

**`Jira 403` on an action** — your Jira account lacks the permission. The app only
offers what Jira says is available, so this usually means the permission was
revoked since the list was loaded. Press `r`.

**"This project has no boards"** — the project has no agile board, or your account
cannot see it. The other three tabs still work.

**The board has columns but no cards** — the board itself returned no issues; it
says so above the columns. Its filter may exclude everything, or the issues may be
in a sprint that has not started. `r` reloads and `b` picks another board. To see
what Jira actually answered, run with `TERMINAL_JIRA_LOG=/tmp/jira.log` and read
that file: each line ends with how many items came back.

**A board column says "(not on the board)"** — those issues are in a status no
column on your board claims, so they are shown in a column of their own rather
than hidden. Either the board's column layout leaves that status out, or your
account cannot read the board's configuration.

**Nothing in the Mine tab** — it filters to issues assigned to you that are not
done. Try Open or All.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for dependencies, build and tests, and
[AGENTS.md](AGENTS.md) if you are working with an AI agent on this codebase.

## License

terminal-jira is free software under the **GNU General Public License, version 3 or
later** — see [LICENSE](LICENSE). You may use, study, share and modify it; if
you distribute a modified version, it has to carry the same freedoms.

It comes with no warranty, to the extent permitted by law.

The libraries it is built on — [FTXUI](https://github.com/ArthurSonzogni/FTXUI),
[nlohmann/json](https://github.com/nlohmann/json) and
[libcurl](https://curl.se/) — are MIT and MIT-derived, and their notices are in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Nothing here requires a paid
or restricted dependency.
