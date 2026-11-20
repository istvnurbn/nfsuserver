#!/usr/bin/env python3
"""Check that race results end up in the stats and track records.

  stats.py SERVER race       two players race in a ranked room and report
  stats.py SERVER cheats     reports that must not count, a quit race
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


def start_race(host, guest, room, game):
    """Both enter the room, the host creates a game for two and the guest
    joins it, which starts the race."""
    for p in (host, guest):
        p.request("move", f"NAME={room}\n", "move")
    host.request("gcre", f"NAME={game}\nROOM={room}\nMAXSIZE=2\nMINSIZE=2\nSYSFLAGS=0\nPARAMS=x", "gcre")
    guest.request("gget", f"NAME={game}", "gget")
    guest.request("gjoi", f"NAME={game}", "gjoi")
    for p in (host, guest):
        p.read(0.5)


def leave(guest, host, game):
    """Both leave the game; the last one out gets no glea back."""
    guest.request("glea", f"NAME={game}", "glea")
    host.send("glea", f"NAME={game}")
    host.read(0.3)


def report(p, rept, names, resu_value):
    names = "".join(f"NAME{i}={n}\n" for i, n in enumerate(names))
    p.request("rank", f"REPT={rept}\n{names}RESU={resu_value}\n", "rank")


def race(server):
    w, l = player(server, WINNER), player(server, LOSER)
    start_race(w, l, "A.Global", "statsrace")
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
    expect([n for n in records if n in LAP_MS], [WINNER, LOSER], "track 1001: winner ahead of loser")
    expect(records[WINNER][0], LAP_MS[WINNER], "winner's best lap in ms")


def cheats(server):
    """Reports that must not count, then one that must."""
    a, b = player(server, "racera"), player(server, "racerb")
    places = [(1, 60000), (2, 61000)]
    names = ["racera", "racerb"]
    before, before_b = circuit(a, "racera"), circuit(a, "racerb")

    report(a, "racera", names, resu(0, places))
    expect(circuit(a, "racera"), before, "a win reported without a race")

    start_race(a, b, "A.Global", "cheats1")
    report(b, "racera", names, resu(0, places))
    expect(circuit(a, "racera"), before, "a win reported for someone else")
    report(a, "racera", names, resu(1, places))
    expect(circuit(a, "racerb"), before_b, "a report whose seed is the other player")
    huge = base64.b64encode(bytes([100, 0, 0, 3, 233, 0, 2]) + bytes(60)).decode()
    report(a, "racera", names, huge)
    expect(circuit(a, "racera"), before, "a report of 100 players")

    # the ranked race above is quit without a report; the next one, in an
    # unranked room, must not count
    leave(b, a, "cheats1")
    start_race(a, b, "E.Global", "cheats2")
    report(a, "racera", names, resu(0, places))
    expect(circuit(a, "racera"), before, "a win in an unranked room after a quit ranked race")

    leave(b, a, "cheats2")
    start_race(a, b, "A.Global", "cheats3")
    report(a, "racera", names, resu(0, places))
    expect(circuit(a, "racera")[1], before[1] + 1, "wins after a correct report")


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
    {"race": race, "cheats": cheats, "restarted": restarted, "files": files, "imported": imported}[step](target)
