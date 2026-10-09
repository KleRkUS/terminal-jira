# Talking to Jira

Everything HTTP lives in `src/jira_client.cpp`. It targets the Jira Cloud
platform REST API v3 and the Jira Software (Agile) API 1.0. When in doubt, check
the official reference rather than guessing a field name:

- [Platform REST API v3](https://developer.atlassian.com/cloud/jira/platform/rest/v3/intro/)
- [Jira Software (Agile) API](https://developer.atlassian.com/cloud/jira/software/rest/intro/)
- [Atlassian Document Format](https://developer.atlassian.com/cloud/jira/platform/apis-documentation/)

## Endpoints in use

| Purpose | Request |
| --- | --- |
| Projects | `GET /rest/api/3/project/search?expand=lead` |
| Boards of a project | `GET /rest/agile/1.0/board?projectKeyOrId=KEY` |
| Board columns | `GET /rest/agile/1.0/board/{id}/configuration` |
| Issues by JQL | `POST /rest/api/3/search/jql` |
| Issues on a board | `GET /rest/agile/1.0/board/{id}/issue` |
| One issue | `GET /rest/api/3/issue/{key}?fields=…` |
| Comments | `GET`/`POST /rest/api/3/issue/{key}/comment` |
| Issue types | `GET /rest/api/3/issue/createmeta/{key}/issuetypes` |
| Priorities | `GET /rest/api/3/priority` |
| Create | `POST /rest/api/3/issue` |
| Edit fields | `PUT /rest/api/3/issue/{key}` |
| Assign | `PUT /rest/api/3/issue/{key}/assignee` |
| Transitions | `GET`/`POST /rest/api/3/issue/{key}/transitions` |
| Assignable users | `GET /rest/api/3/user/assignable/search?issueKey=…` |
| Statuses of a project (list filter) | `GET /rest/api/3/project/{key}/statuses` |
| Assignable users of a project (list filter) | `GET /rest/api/3/user/assignable/search?project=…` |

## Traps

**Two different pagination schemes.** `POST /search/jql` is token-based: the
response carries `nextPageToken`, and its absence means you have the last page —
there is no total, so the UI says "more available" rather than showing a count.
The Agile board endpoint is the older offset style with `startAt`, `maxResults`
and `total`. `IssuePage` carries both (`next_token` and `next_start`) with a
single `is_last` so callers do not care which they got.

The older `GET`/`POST /rest/api/3/search` endpoints are deprecated in favour of
`/search/jql`. Do not reintroduce them.

**Board columns identify statuses by id only.** The configuration response gives
each column a `statuses` array of `{"id": "3"}` with no names, which is why
`Issue` carries `status_id` as well as `status`, and why the board maps cards to
columns by id. Matching on the display name silently puts every card in the wrong
column on any instance with renamed statuses. A card whose `status_id` matches no
column is then resolved by status name, and if that fails too it gets a column of
its own, marked as not being on the board. A card is never dropped: silently
discarding the unmatched ones is how a board full of issues renders as three empty
columns, which is what happens when the configuration is unavailable — it needs
broader permissions on some instances — or when a status was left out of the
column layout.

**Rich text is not a string.** `description` and comment bodies are ADF node
trees on the way in *and* out. Send a plain string and the request is rejected.
`adf::from_text` builds the document; `adf::to_text` flattens one for display. A
Server/DC instance using api/2 would send wiki markup as a plain string, so
`adf::to_text` accepts a string unchanged.

**Unassigning is `null`, not `""`.** `PUT .../assignee` takes
`{"accountId": null}` to clear the field. Detaching a parent is the same shape:
`{"fields": {"parent": null}}`.

**The editable fields are not one endpoint.** The ticket window's field list
looks uniform but every row is its own call. Status goes through
`POST .../transitions` because a workflow step is not a field write; the assignee
has a dedicated `PUT .../assignee`; type, priority, reporter, parent and labels
are all `PUT /issue/{key}` with different `fields` members. The choices come from
five different places too — the issue's transitions, the project's issue types,
this Jira's priorities, `user/assignable/search`, and a JQL search for the other
issues in the project.

Parent candidates are deliberately unfiltered: what may be a parent depends on
the issue's type and on whether the project is team- or company-managed, so the
app offers the project's issues and lets Jira reject the combination it does not
allow.

**Ask Jira what is possible.** Transitions come from the issue's own transitions
endpoint, which already accounts for the workflow and the user's permissions.
Assignees come from `user/assignable/search`, not a list of all users. Never
hard-code a status name or a workflow step.

**Sort and filter in the JQL, not on screen.** The lists arrive 50 at a time,
so sorting the loaded page would put the wrong tickets first. The list query
becomes `status in (…)`, `assignee in (accountIds…) OR assignee is EMPTY`,
`parent in (…)`, `key in (…)` and `summary ~ "…"`, then one `ORDER BY`. Only one
sort field: Jira has ignored the direction of fields that follow `parent` in an
`ORDER BY`. `parent` supports `IN` but not `IS EMPTY`, so there is no "no
parent" filter. The parent choices are the `parent` keys of the loaded rows; one
`key in (…)` search adds their names, and since Jira rejects that whole query if
any one key no longer exists, a failure falls back to bare keys without a popup. `/project/{key}/statuses` is grouped by issue type and repeats a
status for every type using it; the client keeps one of each name.

**There is no portable bulk edit.** Cloud has `/rest/api/3/bulk/issues/...`, but
it is asynchronous, needs the bulk-change global permission, and Server and
Data Center do not have it. Changing several tickets is therefore one
transitions or assignable-users read per ticket, then one write per ticket, sent
in turn on the worker thread while the UI is blocked (`ui::Context::block`).
Tickets can sit on different workflows, so the choices offered are the ones all
of them share, and each ticket moves by its own transition id.

**Report failures verbatim.** `request()` throws `JiraError` carrying the status
code and the unmodified response body (status `0` and the transport error when the
request never arrived). Jira's own `errorMessages` and field-keyed `errors` are
more precise than any paraphrase, so the popup shows the body as it came. Do not
add translated or friendlier error text here.

## Authentication

Chosen by whether `email` is set, so one binary serves both deployments:

- **email set** — HTTP basic with email as the username and an Atlassian API
  token as the password. This is Jira Cloud.
- **email empty** — the token is sent as a bearer token. This is a Server or
  Data Center personal access token.

Both go through libcurl's auth options (`CURLOPT_USERPWD` / `CURLAUTH_BEARER`
with `CURLOPT_XOAUTH2_BEARER`) rather than a hand-built `Authorization` header.
That is deliberate: curl then treats the value as a credential and, with
`CURLOPT_UNRESTRICTED_AUTH` off, will not replay it to a different host if a
request is redirected. A custom header would be replayed.

Related hygiene: plain `http://` is refused unless `TERMINAL_JIRA_ALLOW_HTTP=1`.
A world-readable config file is tightened by the platform module (on Linux, to
mode `600`) with a notice, and `JIRA_TOKEN`/`JIRA_EMAIL` are removed from the
environment of `$EDITOR` and browser children.

## Adding a call

1. Add the method to `JiraClient`, returning a struct from `models.hpp`. Parse
   defensively: `str_or` and `nested` tolerate missing or null fields, which Jira
   produces freely depending on permissions and project configuration.
2. Let failures throw; `ui::async` turns them into a status line.
3. Teach `tests/fake_jira.py` to answer it, and add an assertion to
   `tests/smoke.sh` if the UI depends on it.
