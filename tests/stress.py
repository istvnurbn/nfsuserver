#!/usr/bin/env python3
"""Hammer the lobby from several threads at once (audit #4).

  stress.py SERVER [SECONDS [PAUSE]]

Hosts create games and drop their connection, which frees the game in the
server's IO thread, while other players look up and join those same games,
and the status port (10980) is read over and over. PAUSE (seconds,
default 0) slows down the status port readers. Meant for a build with
-fsanitize=address: it reports the first use of freed memory. Exits 1 if the
server stops answering.
"""

import socket
import sys
import threading
import time

from lobby_addresses import Player

ROOM = "A.Global"


def logged_in(server, name):
    p = Player(name, "0.0.0.0", server, 10901, "")
    p.request("addr", f"ADDR={server}\nPORT=3658\n", "skey")
    p.log_in()
    p.request("cper", f"PERS={name}\n", "cper", "cperdupl")
    p.request("pers", f"PERS={name}\n", "pers")
    p.request("move", f"NAME={ROOM}\n", "move")
    return p


def churn(server, n, until):
    """Create a game, then vanish."""
    while time.time() < until:
        try:
            p = logged_in(server, f"churn{n}")
            p.send("gcre", f"NAME=g{n}\nROOM={ROOM}\nMAXSIZE=4\nMINSIZE=2\nSYSFLAGS=0\nPARAMS=x")
            time.sleep(0.02)
            p.sock.close()
        except (OSError, SystemExit):
            pass


def join(server, n, games, until):
    """Look up and join the churners' games, leave again."""
    try:
        p = logged_in(server, f"joiner{n}")
        while time.time() < until:
            for g in range(games):
                p.send("gget", f"NAME=g{g}")
                p.send("gjoi", f"NAME=g{g}")
                p.send("glea", f"NAME=g{g}")
            p.read(0.01)
            p.inbox.clear()
    except (OSError, SystemExit):
        pass


def poll(server, until, pause):
    """Read the room list from the status port."""
    while time.time() < until:
        try:
            with socket.create_connection((server, 10980), timeout=2) as s:
                while s.recv(4096):
                    pass
        except OSError:
            pass
        time.sleep(pause)  # a small VM's conntrack table fills up without one


def main():
    server = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 15
    pause = float(sys.argv[3]) if len(sys.argv) > 3 else 0
    until = time.time() + seconds
    churners, joiners = 4, 3
    threads = [threading.Thread(target=churn, args=(server, n, until)) for n in range(churners)]
    threads += [threading.Thread(target=join, args=(server, n, churners, until)) for n in range(joiners)]
    threads += [threading.Thread(target=poll, args=(server, until, pause)) for _ in range(2)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    try:
        logged_in(server, "survivor")
        print(f"ok  server still answers after {seconds:.0f} s of stress")
    except (OSError, SystemExit):
        sys.exit("the server no longer answers")


if __name__ == "__main__":
    main()
