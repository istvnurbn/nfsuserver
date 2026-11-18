#!/usr/bin/env python3
"""Send the lobby server messages that a game never sends, and fail if the
server stops answering.

  bad_input.py SERVER

Each message is only its 12 byte header, which used to be enough to crash the
server: before login, one of every command that the login screen does not
use; after login but outside a room, one of every game command that needs a
room.
"""

import sys
import time

from lobby_addresses import Player

# the login screen sends addr, skey, sele, news, acct, auth and ~png
AFTER_LOGIN = {"user", "pers", "cper", "dper", "move", "room", "mesg", "rank", "auxi", "snap"}
GAMES = {"gcre", "gget", "gjoi", "glea", "gdel", "gsta"}


def header_only(player, command):
    player.sock.sendall(command.encode() + bytes(4) + (12).to_bytes(4, "big"))


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

    Player("after", "0.0.0.0", server, 10901, tag).request(
        "addr", f"ADDR={server}\nPORT=3658\n", "skey"
    )
    print("the server is still up")


if __name__ == "__main__":
    main()
