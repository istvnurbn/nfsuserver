#!/usr/bin/env python3
"""Check that race results end up in the stats and track records.

  stats.py SERVER race       two players race in a ranked room and report
  stats.py SERVER restarted  the results of "race" are still there; a persona
                             deleted takes its stats and records with it
  stats.py DIR files         write an old-style stat.dat and s1001.dat to DIR
  stats.py SERVER imported   the stats and records from those files are listed
"""

import base64
import struct
import sys

from accounts import expect
from lobby_addresses import Player

WINNER, LOSER = "winner", "loser"
TRACK, LAPS = 1001, 2  # circuit Market Street: a win pays 1.3 x 10% of the opponent's REP
LAP_MS = {WINNER: 61500, LOSER: 63250}
# the persona and numbers in the old files that "files" writes
OLD = "oldpers1"


def resu(reporter, places, track=TRACK, laps=LAPS, car=5):
    """RESU as the game sends it in rank: a header, then one block per player
    in seed order with place, finish mark, best lap and car."""
    block = 58 + 8 * laps
    b = bytearray(7 + block * len(places))
    b[0:7] = bytes([len(places), 0, reporter]) + struct.pack(">H", track) + bytes([0, laps])
    for seed, (place, lap_ms) in enumerate(places):
        o = 7 + block * seed
        b[o], b[o + 2], b[o + 3] = seed, place, 1  # finish mark 1: finished
        b[o + 12 : o + 16] = struct.pack(">f", lap_ms / 1000)
        b[o + 12 + laps * 8 + 42] = car
    # the server's decoder drops the last byte of the final group
    return base64.b64encode(bytes(b) + bytes(3)).decode()


def snap(p, index):
    """The rank list INDEX as {persona: [numbers]}, in the order sent."""
    first = len(p.inbox)
    p.send("snap", f"INDEX={index}\nCHAN=0\nSTART=0\nRANGE=100\n")
    p.read(0.5)
    return {f["N"]: [int(x, 16) if x else None for x in f["S"].split(",")]
            for c, f in p.inbox[first:] if c == "+snp"}


def circuit(p, persona):
    """rating, wins, losses, disconnects, REP of the circuit list (INDEX 2)."""
    s = snap(p, 2).get(persona)
    return s and s[7:12]


def player(server, name):
    p = Player(name, "0.0.0.0", server, 10901, "")
    p.request("addr", f"ADDR={server}\nPORT=3658\n", "skey")
    p.log_in()
    p.request("cper", f"PERS={name}\n", "cper", "cperdupl")
    p.request("pers", f"PERS={name}\n", "pers")
    return p


def race(server):
    w, l = player(server, WINNER), player(server, LOSER)
    for p in (w, l):
        p.request("move", "NAME=A.Global\n", "move")
    w.request("gcre", "NAME=statsrace\nROOM=A.Global\nMAXSIZE=2\nMINSIZE=2\nSYSFLAGS=0\nPARAMS=x", "gcre")
    l.request("gget", "NAME=statsrace", "gget")
    l.request("gjoi", "NAME=statsrace", "gjoi")
    for p in (w, l):
        p.read(0.5)
    places = [(1, LAP_MS[WINNER]), (2, LAP_MS[LOSER])]
    # the winner reports first: the old server saved the loss only on the next win
    for seed, p, persona in ((0, w, WINNER), (1, l, LOSER)):
        p.request("rank", f"REPT={persona}\nNAME0={WINNER}\nNAME1={LOSER}\nRESU={resu(seed, places)}\n", "rank")
    check(w)


def check(p):
    # REP starts at 100: the win pays round(100 x 1.3 x 0.1) = 13, the loss
    # takes 10 but REP stays at least 100
    expect(circuit(p, WINNER)[1:5], [1, 0, 0, 113], "winner: wins, losses, disconnects, REP")
    expect(circuit(p, LOSER)[1:5], [0, 1, 0, 100], "loser: wins, losses, disconnects, REP")
    records = snap(p, 6)  # INDEX 6: the best laps on track 1001
    expect(list(records)[:2], [WINNER, LOSER], "track 1001 record holders, best first")
    expect(records[WINNER][0], LAP_MS[WINNER], "winner's best lap in ms")


def restarted(server):
    p = player(server, WINNER)
    check(p)
    # the loser deletes the persona: its stats and record go with it
    l = player(server, LOSER)
    l.request("dper", f"PERS={LOSER}\n", "dper")
    expect(circuit(l, LOSER), None, "deleted persona in the circuit list")
    expect(LOSER in snap(l, 6), False, "deleted persona among the track 1001 records")


def files(directory):
    """stat.dat: Name[16] and 35 ints, 7 per mode (rating, wins, losses,
    disconnects, REP, opponents' REP, opponents' rating) for all, circuit,
    sprint, drag, drift. s1001.dat: Name[16], lap ms, car, dir. A record of
    a persona that doesn't exist is dropped by the import."""
    modes = [[9999, 7, 2, 0, 900, 101, 101], [9999, 7, 2, 0, 5000, 101, 101]] + [[9999, 0, 0, 0, 100, 101, 101]] * 3
    with open(f"{directory}/stat.dat", "wb") as f:
        for name in (OLD, "ghost"):
            f.write(struct.pack("16s35i", name.encode(), *sum(modes, [])))
    with open(f"{directory}/s1001.dat", "wb") as f:
        for name in (OLD, "ghost"):
            f.write(struct.pack("16s3i", name.encode(), 70000, 3, 0))


def imported(server):
    p = player(server, "viewer")
    expect(circuit(p, OLD)[1:5], [7, 2, 0, 5000], "imported circuit stats")
    expect(circuit(p, "ghost"), None, "stats of a persona that doesn't exist")
    expect(circuit(p, "oldpers2")[1:5], [0, 0, 0, 100], "imported persona missing from stat.dat starts fresh")
    expect(snap(p, 6).get(OLD, [None])[0], 70000, "imported best lap on track 1001")


if __name__ == "__main__":
    target, step = sys.argv[1:3]
    {"race": race, "restarted": restarted, "files": files, "imported": imported}[step](target)
