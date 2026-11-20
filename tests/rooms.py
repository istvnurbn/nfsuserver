#!/usr/bin/env python3
"""Check moving between rooms and who may start or delete a game.

  rooms.py SERVER

Reads the room lists from the status port (10980) to see who is where.
"""

import re
import socket
import sys

from accounts import expect
from stats import player


def rooms(server):
    """{room: [personas]} as the status port lists them."""
    data = b""
    with socket.create_connection((server, 10980), timeout=5) as s:
        while chunk := s.recv(4096):
            data += chunk
    text = data.decode("latin-1").split("~~~", 1)[1]
    return {name: [p for p in people.split("|") if p] for name, _, people in re.findall(r"(.*?)\|(\d+)\|\[(.*?)\]", text)}


def where(server, persona):
    return [room for room, people in rooms(server).items() if persona in people]


def main():
    server = sys.argv[1]
    p, q = player(server, "roomer"), player(server, "roomguest")

    p.request("move", "NAME=A.Global\n", "move")
    p.request("move", "NAME=B.Global\n", "move")
    expect(where(server, "roomer"), ["B.Global"], "rooms after moving from A.Global to B.Global")

    reply = p.request("move", "NAME=\n", "move")
    expect((reply.get("IDENT"), "T" in reply), ("0", False), "the reply to leaving a room (no chat text in it)")
    expect(reply.get("LIDENT", "0") != "0", True, "the reply names the room left")
    expect(where(server, "roomer"), [], "rooms after leaving")

    for r in (p, q):
        r.request("move", "NAME=A.Global\n", "move")
    p.request("gcre", "NAME=hostgame\nROOM=A.Global\nMAXSIZE=4\nMINSIZE=2\nSYSFLAGS=0\nPARAMS=x", "gcre")
    q.send("gdel", "NAME=hostgame")
    q.read(0.3)
    expect("IDENT" in q.request("gget", "NAME=hostgame", "gget"), True, "the game after a gdel by another player")
    q.send("gsta", "NAME=hostgame")
    q.read(0.3)
    expect([c for c, _ in q.inbox[-5:] if c.startswith("gsta")], [], "replies to a gsta by another player")
    first = len(p.inbox)
    p.send("gdel", "NAME=hostgame")
    p.read(0.3)
    expect("gdel" in [c for c, _ in p.inbox[first:]], True, "the host deletes the game")


if __name__ == "__main__":
    main()
