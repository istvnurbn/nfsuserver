#!/usr/bin/env python3
"""Send the lobby server messages that a game never sends, and fail if the
server stops answering.

  bad_input.py SERVER

First messages that are only their 12 byte header, which used to be enough to
crash the server: before login, one of every command that the login screen
does not use; after login but outside a room, one of every game command that
needs a room. Then every command with fields that are missing, unterminated
or too long for the server, before login and in a room.
"""

import sys
import time

from lobby_addresses import Player

# the login screen sends addr, skey, sele, news, acct, auth and ~png
LOGIN_SCREEN = ["addr", "skey", "sele", "news", "acct", "auth", "~png"]
AFTER_LOGIN = {"user", "pers", "cper", "dper", "move", "room", "mesg", "rank", "auxi", "snap"}
GAMES = {"gcre", "gget", "gjoi", "glea", "gdel", "gsta"}

# every key a handler reads
KEYS = [
    "NAME", "PERS", "TEXT", "PRIV", "ROOM", "MAXSIZE", "MINSIZE", "SYSFLAGS",
    "PARAMS", "REPT", "NAME0", "NAME1", "NAME2", "NAME3", "RESU", "ADDR",
    "PORT", "INDEX", "CHAN", "START", "RANGE", "VERS",
]
# the game commands go first, because move and pers can take the player out
# of the room or the persona
IN_ROOM = [
    "gget", "gjoi", "glea", "gdel", "gsta", "gcre", "auxi", "mesg", "rank",
    "room", "snap", "user", "news", "sele", "skey", "~png", "acct", "cper",
    "dper", "addr", "auth", "move", "pers",
]


def header_only(player, command):
    player.sock.sendall(command.encode() + bytes(4) + (12).to_bytes(4, "big"))


def unterminated(player, command, body):
    """No newline after the last value and no zero byte at the end."""
    data = body.encode()
    player.sock.sendall(command.encode() + bytes(4) + (12 + len(data)).to_bytes(4, "big") + data)


def bad_bodies():
    # "A" decodes from base64 to zero bits, so auxi does not take it for a cheat
    yield "A" * 900
    for key in KEYS:
        yield f"{key}={'A' * 900}"


def log_in(player, server):
    player.request("addr", f"ADDR={server}\nPORT=3658\n", "skey")
    player.request("acct", f"NAME={player.user}\nPASS=x\n", "acct", "acctdupl")
    player.request("auth", f'NAME={player.user}\nPASS=x\nVERS="pc/1.4"\n', "auth")


def main():
    server = sys.argv[1]
    tag = f"{int(time.time() * 10) % 10000:04d}"

    # read() exits as soon as the server closes the connection
    stranger = Player("stranger", "0.0.0.0", server, 10901, tag)
    for command in sorted(AFTER_LOGIN | GAMES):
        header_only(stranger, command)
    stranger.read(1)

    roomless = Player("roomless", "0.0.0.0", server, 10901, tag)
    log_in(roomless, server)
    for command in sorted(GAMES - {"gcre"}):
        header_only(roomless, command)
    roomless.read(1)

    newcomer = Player("newcomer", "0.0.0.0", server, 10901, tag)
    for command in LOGIN_SCREEN:
        for body in bad_bodies():
            unterminated(newcomer, command, body)
    newcomer.read(1)

    player = Player("player", "0.0.0.0", server, 10901, tag)
    log_in(player, server)
    player.request("cper", f"PERS={player.user}\n", "cper")
    player.request("pers", f"PERS={player.user}\n", "pers")
    player.request("room", f"NAME={player.user}\n", "room")
    # lookups that find nothing, and a race result without its fields
    player.send("gsta", "NAME=nosuchgame\n")
    player.send("gcre", "NAME=g\nROOM=nosuchroom\nMAXSIZE=2\nMINSIZE=2\nSYSFLAGS=0\nPARAMS=x\n")
    player.send("rank", f"REPT={player.user}\nNAME0={player.user}\n")
    for command in IN_ROOM:
        for body in bad_bodies():
            unterminated(player, command, body)
    player.read(1)

    Player("after", "0.0.0.0", server, 10901, tag).request(
        "addr", f"ADDR={server}\nPORT=3658\n", "skey"
    )
    print("the server is still up")


if __name__ == "__main__":
    main()
