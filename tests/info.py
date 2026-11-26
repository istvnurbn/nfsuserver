#!/usr/bin/env python3
"""Check the infoserver answers (statsport 10880) against the JSON API.

  info.py SERVER

Run after the stats tests, so that there are ranks and track records.
"""

import http.client
import json
import socket
import sys

from accounts import expect

UNKNOWN = "Error: Unrecognized request"


def ask(server, request):
    """One request per connection; the server answers and closes."""
    with socket.create_connection((server, 10880), timeout=5) as s:
        s.sendall(request)
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                return data.decode()
            data += chunk


def api(server, path):
    c = http.client.HTTPConnection(server, 10990, timeout=5)
    c.request("GET", path)
    out = json.loads(c.getresponse().read())
    c.close()
    return out


def rows(answer):
    return [r.split("|") for r in answer.split("~")] if answer else []


def main():
    server = sys.argv[1]
    expect(ask(server, b"ping"), "pong", "ping")
    expect(ask(server, b"ping\r\n"), "pong", "ping with a line end")
    for bad in (b"rank:bogus", b"rank", b"perf:9999", b"perf:abc", b"perf:", b"hello", b"GET / HTTP/1.0\r\n\r\n", b"\xff\xfe"):
        expect(ask(server, bad), UNKNOWN, f"refused: {bad!r}")

    ranked = 0
    for mode in ("all", "circuit", "sprint", "drag", "drift"):
        got = rows(ask(server, f"rank:{mode}".encode()))
        expect(all(len(r) == 8 for r in got), True, f"rank:{mode} rows have 8 fields")
        expect(all(int(r[2]) + int(r[3]) > 0 for r in got), True, f"rank:{mode} lists only players who raced")
        reps = [int(r[5]) for r in got]
        expect(reps == sorted(reps, reverse=True), True, f"rank:{mode} sorted by REP")
        # the leaderboard of the JSON API is the same list, unfiltered and cut at 100
        board = [p for p in api(server, f"/api/leaderboard?mode={mode}")["players"] if p["wins"] + p["losses"] > 0]
        mine = [(r[0], int(r[1]), int(r[2]), int(r[3]), int(r[4]), int(r[5])) for r in got[:len(board)]]
        theirs = [(p["persona"], p["rating"], p["wins"], p["losses"], p["disconnects"], p["rep"]) for p in board]
        expect(mine, theirs, f"rank:{mode} matches the leaderboard")
        ranked += len(got)
    expect(ranked > 0, True, "some ranks to compare")

    recorded = 0
    for track in api(server, "/api/records")["tracks"]:
        got = rows(ask(server, f"perf:{track['track']}".encode()))
        expect(all(len(r) == 4 for r in got), True, f"perf:{track['track']} rows have 4 fields")
        mine = [(r[0], int(r[1]), int(r[2]), int(r[3])) for r in got[:len(track["records"])]]
        theirs = [(r["persona"], r["score"], r["car"], r["dir"]) for r in track["records"]]
        expect(mine, theirs, f"perf:{track['track']} matches the track records")
        recorded += len(got)
    expect(recorded > 0, True, "some track records to compare")


if __name__ == "__main__":
    main()
