#!/usr/bin/env python3
"""Check the JSON API for the website (port 10990).

  api.py SERVER

Runs after stats.py SERVER race, whose winner and loser it expects in the
circuit leaderboard and on track 1001.
"""

import http.client
import json
import sys

from accounts import expect
from stats import LAP_MS, LOSER, WINNER, player

# as the game sends it, %3d and %0a included; MYIP is the host's address
PARAMS = "TRACK%3d1102%0aDIR%3d0%0aLAPS%3d2%0aMYIP%3d167774740"
GAME = 'api"race\\1'  # a quote and a backslash must come out escaped


def get(server, path, method="GET"):
    c = http.client.HTTPConnection(server, 10990, timeout=5)
    c.request(method, path)
    r = c.getresponse()
    body = r.read().decode()
    c.close()
    return r.status, body


def get_json(server, path):
    status, body = get(server, path)
    expect(status, 200, f"status of {path}")
    return json.loads(body), body


def races(server):
    return [r for r in get_json(server, "/api/races")[0]["races"] if r["name"] == GAME]


def main():
    server = sys.argv[1]

    status, _ = get_json(server, "/api/status")
    expect((status["name"], status["address"]), ("VM test server", "203.0.113.7"), "server name and address")
    europe = [r for r in status["rooms"] if r["name"] == "Europe"]
    expect([(r["type"], r["mode"], r["ranked"]) for r in europe], [("A", "circuit", True)], "room A.Europe")

    board, _ = get_json(server, "/api/leaderboard?mode=circuit")
    names = [p["persona"] for p in board["players"]]
    expect(names.index(WINNER) < names.index(LOSER), True, "winner ranked above loser (by REP)")
    expect([p["rep"] for p in board["players"] if p["persona"] == WINNER], [113], "winner's circuit REP")
    expect(get(server, "/api/leaderboard?mode=nope")[0], 400, "an unknown mode")

    tracks, _ = get_json(server, "/api/records")
    expect(len(tracks["tracks"]), 30, "tracks listed")
    first = [t for t in tracks["tracks"] if t["track"] == 1001][0]["records"]
    expect([(r["persona"], r["score"]) for r in first if r["persona"] in LAP_MS],
           [(WINNER, LAP_MS[WINNER]), (LOSER, LAP_MS[LOSER])], "best laps on track 1001")

    host, guest = player(server, "apihost"), player(server, "apiguest")
    host.request("auxi", "TEXT=05SAAAAAGAAAAAAAAAAAAAAAAAAAAAAAAAAAAA%3d\n", "auxi")  # S: Nissan 240SX
    for p in (host, guest):
        p.request("move", "NAME=B.Global\n", "move")
    host.request("gcre", f"NAME={GAME}\nROOM=B.Global\nMAXSIZE=2\nMINSIZE=2\nSYSFLAGS=0\nPARAMS={PARAMS}", "gcre")
    got = races(server)
    expect([(r["track"], r["laps"], r["dir"], r["started"], r["room"]["mode"]) for r in got],
           [(1102, 2, 0, False, "sprint")], "the new game")
    expect(got[0]["players"], [{"persona": "apihost", "car": 18}], "its host and car")
    expect("167774740" in get(server, "/api/races")[1], False, "the host's address (MYIP) kept out")

    guest.request("gget", f"NAME={GAME}", "gget")
    guest.request("gjoi", f"NAME={GAME}", "gjoi")
    for p in (host, guest):
        p.read(0.5)
    got = races(server)
    expect([(r["started"], [p["persona"] for p in r["players"]]) for r in got],
           [(True, ["apihost", "apiguest"])], "the race after the guest joined")

    expect(get(server, "/api/nothing")[0], 404, "an unknown path")
    expect(get(server, "/api/status", "POST")[0], 405, "a POST")


if __name__ == "__main__":
    main()
