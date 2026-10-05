#!/usr/bin/env python3
# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""A stand-in for Jira Cloud, enough to drive the whole UI offline.

It answers the handful of endpoints terminal-jira calls, with deterministic data, and
records what the client sent so tests can assert on it:

    auth.log    one line per request: method and Authorization header
    writes.log  one line per mutation: method, path and body

Usage:
    python3 tests/fake_jira.py [--port 8723] [--log-dir .]
"""
import argparse
import json
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROJECTS = [
    {"id": "1", "key": "ENG", "name": "Engineering", "lead": {"displayName": "Ada Lovelace"}},
    {"id": "2", "key": "OPS", "name": "Operations", "lead": {"displayName": "Grace Hopper"}},
]

# (status id, status name) in board-column order.
STATUSES = [("1", "To Do"), ("3", "In Progress"), ("5", "Done")]

PRIORITIES = [
    {"id": "1", "name": "Highest"},
    {"id": "2", "name": "High"},
    {"id": "3", "name": "Medium"},
    {"id": "4", "name": "Low"},
]

BOARDS = [
    {"id": 10, "name": "ENG Sprint Board", "type": "scrum"},
    {"id": 11, "name": "ENG Kanban", "type": "kanban"},
]

LOG_DIR = "."
FAIL = set()  # any of {"reads", "writes"}, set by --fail
BOARD = "normal"  # see --board


def log(name, line):
    with open(os.path.join(LOG_DIR, name), "a") as handle:
        handle.write(line + "\n")


def adf(text):
    return {
        "type": "doc",
        "version": 1,
        "content": [{"type": "paragraph", "content": [{"type": "text", "text": text}]}],
    }


def issue(n):
    """A stable issue whose status cycles through the three board columns."""
    status_id, status_name = STATUSES[n % 3]
    if BOARD == "extra-status" and n % 2 == 0:
        # A status the board's columns do not include, as a workflow status that
        # was never added to the board would be.
        status_id, status_name = "99", "Backlog"
    return {
        "key": f"ENG-{n}",
        "fields": {
            "summary": f"Make widget {n} faster than before",
            "status": {"id": status_id, "name": status_name},
            "issuetype": {"name": "Task" if n % 2 else "Bug"},
            "priority": {"name": "High" if n % 3 == 0 else "Medium"},
            "assignee": None if n % 4 == 0 else {"displayName": "Ada Lovelace"},
            "reporter": {"displayName": "Grace Hopper"},
            "updated": "2026-10-05T09:00:00.000+0400",
            "parent": None,
            "labels": ["perf", "tui"],
            "description": adf(f"Details for issue {n}."),
        },
    }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # keep the test output clean

    def reply(self, payload):
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_body(self):
        return self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()

    def fail(self, status=403):
        """Reply like Jira does when it rejects a request."""
        body = json.dumps({
            "errorMessages": ["You do not have permission to perform this action."],
            "errors": {"assignee": "Field 'assignee' cannot be set."},
        }).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def should_fail(self, kind):
        return kind in FAIL

    def do_GET(self):
        log("auth.log", f"{self.command} {self.headers.get('Authorization')}")
        path = self.path

        if self.should_fail("reads"):
            return self.fail(500)
        if "/project/search" in path:
            return self.reply({"values": PROJECTS, "isLast": True})
        if "/board?" in path:
            return self.reply({"values": BOARDS, "isLast": True})
        if "/configuration" in path:
            # renamed-ids: the columns name statuses this board's issues do not
            # have, which is what a board whose statuses were re-created looks
            # like. The column names still match, so cards must still land.
            shift = "9" if BOARD == "renamed-ids" else ""
            columns = [{"name": name, "statuses": [{"id": shift + sid}]} for sid, name in STATUSES]
            return self.reply({"columnConfig": {"columns": columns}})
        if re.search(r"/board/\d+/issue", path):
            issues = [issue(n) for n in range(1, 13)]
            return self.reply({"issues": issues, "total": len(issues), "startAt": 0})
        if "/transitions" in path:
            return self.reply({"transitions": [
                {"id": "11", "name": "Start", "to": {"name": "In Progress"}},
                {"id": "21", "name": "Finish", "to": {"name": "Done"}},
            ]})
        if "/assignable/search" in path:
            return self.reply([
                {"accountId": "a1", "displayName": "Ada Lovelace"},
                {"accountId": "a2", "displayName": "Grace Hopper"},
            ])
        if "/comment" in path:
            return self.reply({"comments": [{
                "author": {"displayName": "Grace Hopper"},
                "created": "2026-10-05T10:00:00.000+0400",
                "body": adf("Looks good to me."),
            }]})
        if path.endswith("/rest/api/3/priority"):
            return self.reply(PRIORITIES)
        if "/issuetypes" in path:
            return self.reply({"issueTypes": [
                {"id": "10001", "name": "Task", "subtask": False},
                {"id": "10002", "name": "Bug", "subtask": False},
            ]})
        if re.search(r"/issue/[A-Z]+-\d+", path):
            number = int(re.search(r"-(\d+)", path).group(1))
            return self.reply(issue(number))
        return self.reply({})

    def do_POST(self):
        log("auth.log", f"{self.command} {self.headers.get('Authorization')}")
        body = self.read_body()
        if "/search/jql" in self.path:
            # One page only: no nextPageToken means this is the last page.
            return self.reply({"issues": [issue(n) for n in range(1, 9)]})
        log("writes.log", f"POST {self.path} {body}")
        if self.should_fail("writes"):
            return self.fail()
        if self.path.endswith("/rest/api/3/issue"):
            return self.reply({"key": "ENG-999"})
        return self.reply({})

    def do_PUT(self):
        log("auth.log", f"{self.command} {self.headers.get('Authorization')}")
        log("writes.log", f"PUT {self.path} {self.read_body()}")
        if self.should_fail("writes"):
            return self.fail()
        self.reply({})


def main():
    global LOG_DIR, FAIL, BOARD
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8723)
    parser.add_argument("--log-dir", default=".")
    parser.add_argument("--fail", default="",
                        help="comma separated: reads, writes — reject those requests, "
                             "to exercise the error popups")
    parser.add_argument("--board", default="normal",
                        choices=["normal", "renamed-ids", "extra-status"],
                        help="how the board columns relate to the issue statuses: "
                             "matching ids, ids that match nothing, or issues in a "
                             "status the columns leave out")
    args = parser.parse_args()
    LOG_DIR = args.log_dir
    FAIL = {part for part in args.fail.split(",") if part}
    BOARD = args.board

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"fake Jira listening on http://127.0.0.1:{args.port}"
          + (f" (failing: {', '.join(sorted(FAIL))})" if FAIL else ""), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
