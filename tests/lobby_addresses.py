#!/usr/bin/env python3
"""Check which address of each player the lobby server hands to the others.

The games talk to each other directly, and it is the server that tells every
player where the others are. This script plays the part of several games: each
player connects from its own source address, logs in, they all enter one room
and start a race together. It then reports the addresses every player was told
and, if --sees is given, compares them with what is expected.

  lobby_addresses.py \\
      --player here:127.0.0.1:127.0.0.1 \\
      --player lan:192.168.77.10:192.168.77.1 \\
      --player out:198.51.100.50:192.168.77.1 \\
      --sees lan here=192.168.77.1 lan=192.168.77.10 out=198.51.100.50 \\
      --sees out here=203.0.113.7 lan=203.0.113.7 out=198.51.100.50

A player is NAME:SOURCE:SERVER: connect from address SOURCE to the server at
address SERVER. Both have to be addresses of the machine the script runs on
for more than one of them to work, so this is meant for a machine (or network
namespace) that carries the addresses of a whole made up network.
"""

import argparse
import socket
import struct
import sys
import time


class Player:
    def __init__(self, name, source, server, port, tag):
        self.name = name
        self.source = source
        # one name for the account and for the persona of the player
        self.user = f"{name}{tag}"
        self.inbox = []
        self.pending = b""
        self.sock = socket.create_connection(
            (server, port), timeout=5, source_address=(source, 0)
        )

    def send(self, command, body):
        """A message is the command, four unused bytes, the total length and
        the parameters, which end with a zero byte."""
        data = body.encode("latin-1") + b"\0"
        header = command.encode() + bytes(4) + struct.pack(">I", 12 + len(data))
        self.sock.sendall(header + data)

    def read(self, seconds):
        """Collect what arrives within the given time."""
        deadline = time.monotonic() + seconds
        while (left := deadline - time.monotonic()) > 0:
            self.sock.settimeout(left)
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                sys.exit(f"{self.name}: the server closed the connection")
            self.pending += chunk
            while len(self.pending) >= 12:
                (length,) = struct.unpack(">I", self.pending[8:12])
                if len(self.pending) < length:
                    break
                message, self.pending = self.pending[:length], self.pending[length:]
                fields = {}
                # the server separates its parameters with tabs
                for pair in message[12:].rstrip(b"\0").split(b"\t"):
                    key, _, value = pair.decode("latin-1").partition("=")
                    fields[key] = value
                command = message[:8].rstrip(b"\0").decode("latin-1")
                self.inbox.append((command, fields))

    def request(self, command, body, *replies):
        """Send a message and wait for one of the given replies to it."""
        first = len(self.inbox)
        self.send(command, body)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.read(0.05)
            for reply, fields in self.inbox[first:]:
                if reply in replies:
                    return fields
        got = ", ".join(reply for reply, _ in self.inbox[first:]) or "nothing"
        sys.exit(f"{self.name}: no {' or '.join(replies)} after {command}, got {got}")

    def told(self):
        """Yield (message, persona, address) for every address of a player
        this player has been sent."""
        for command, fields in self.inbox:
            # messages about a game: OPPO0, ADDR0, OPPO1, ADDR1, ...
            for key, persona in fields.items():
                if key.startswith("OPPO") and "ADDR" + key[4:] in fields:
                    yield command, persona, fields["ADDR" + key[4:]]
            if command in ("+usr", "+who") and "N" in fields and "A" in fields:
                yield command, fields["N"], fields["A"]
            if command == "+rom" and "H" in fields and "A" in fields:
                yield command, fields["H"], fields["A"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", type=int, default=10901)
    parser.add_argument(
        "--player",
        action="append",
        required=True,
        metavar="NAME:SOURCE:SERVER",
        help="the first player creates the room and hosts the race",
    )
    parser.add_argument(
        "--sees",
        action="append",
        nargs="+",
        default=[],
        metavar=("PLAYER", "NAME=ADDRESS"),
        help="the address PLAYER has to be given for the player NAME",
    )
    args = parser.parse_args()

    # accounts stay on the server, so use different names on every run
    tag = f"{int(time.time() * 10) % 10000:04d}"
    players = [Player(*spec.split(":"), args.port, tag) for spec in args.player]
    if len(players) < 2:
        sys.exit("it takes at least two players")
    names = {player.user: player.name for player in players}

    for player in players:
        player.request("addr", f"ADDR={player.source}\nPORT=3658\n", "skey")
        player.request("acct", f"NAME={player.user}\nPASS=x\n", "acct", "acctdupl")
        # the sixth character of the version is the one the server looks at
        player.request("auth", f'NAME={player.user}\nPASS=x\nVERS="pc/1.4"\n', "auth")
        player.request("cper", f"PERS={player.user}\n", "cper", "cperdupl")
        player.request("pers", f"PERS={player.user}\n", "pers")

    host, guests = players[0], players[1:]
    room, game = f"T{tag}", f"g{tag}"

    # a new room is announced to everybody on the server
    host.request("room", f"NAME={room}", "room")
    host.request(
        "gcre",
        f"NAME={game}\nROOM={room}\nMAXSIZE={len(players)}\nMINSIZE=2\nSYSFLAGS=0\nPARAMS=x",
        "gcre",
    )

    # whoever enters the room is told about the players and the game in it;
    # the race starts by itself once the game is full
    for guest in guests:
        guest.request("move", f"NAME={room}\n", "move")
        guest.request("gget", f"NAME={game}", "gget")
        guest.request("gjoi", f"NAME={game}", "gjoi")
    for player in players:
        player.read(0.5)

    # one player leaves the game and the connection of the host breaks: the
    # others are told about the game again both times
    guests[-1].request("glea", f"NAME={game}", "glea")
    host.sock.close()
    for guest in guests:
        guest.read(0.5)

    expected = {who: dict(pair.split("=") for pair in pairs) for who, *pairs in args.sees}
    failures = []

    for player in players:
        told = {}  # name -> address -> messages it came in
        started = set()
        for command, persona, address in player.told():
            if persona not in names:
                continue
            told.setdefault(names[persona], {}).setdefault(address, []).append(command)
            if command == "+ses":
                started.add(names[persona])

        print(f"{player.name} ({player.source}) is told:")
        for name in (other.name for other in players):
            for address, commands in told.get(name, {}).items():
                wanted = expected.get(player.name, {}).get(name)
                wrong = wanted is not None and address != wanted
                seen = " ".join(sorted(set(commands)))
                print(f"  {name:8} {address:16} {'WRONG, expected ' + wanted if wrong else ''}  [{seen}]")
                if wrong:
                    failures.append(f"{player.name} was told {address} for {name}, expected {wanted}")

        # the start of the race is what the games connect to each other with
        missing = {other.name for other in players} - started
        if missing:
            failures.append(f"{player.name} got no race start (+ses) with {', '.join(sorted(missing))}")

    for failure in failures:
        print(f"FAILED: {failure}", file=sys.stderr)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
