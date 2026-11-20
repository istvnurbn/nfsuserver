#!/usr/bin/env python3
"""Check sign-up, login and personas against a lobby server.

  accounts.py SERVER new        sign up, name and password rules, wrong passwords, locks
  accounts.py SERVER restarted  what "new" created is still there
  accounts.py FILE rusers       write an old-style rusers.dat to FILE
  accounts.py SERVER imported   the accounts from that file can log in
"""

import struct
import sys

from lobby_addresses import Player, encode_pass

# account, password, persona that "new" creates and "restarted" checks
KEEPER = ("keeper", "keep1234", "keeperp")
# account and personas in the rusers.dat that "rusers" writes
OLD = ("oldtimer", ["oldpers1", "oldpers2"])


def connect(server, source="0.0.0.0"):
    """source picks the address the server sees; all of 127/8 is this machine."""
    player = Player("t", source, server, 10901, "")
    player.request("addr", f"ADDR={server}\nPORT=3658\n", "skey")
    return player


def reply(player, command, body):
    """The first answer to a message, whatever it is. Messages starting with
    + are news for every player (a room gone, a player in), not answers."""
    first = len(player.inbox)
    player.send(command, body)
    for _ in range(100):
        player.read(0.05)
        answers = [c for c, _ in player.inbox[first:] if not c.startswith("+")]
        if answers:
            return answers[0]
    sys.exit(f"no answer to {command} {body!r}")


def expect(got, wanted, what):
    if got != wanted:
        sys.exit(f"{what}: expected {wanted}, got {got}")
    print(f"ok  {what}: {got}")


def acct(player, name, password):
    return reply(player, "acct", f"NAME={name}\nPASS={encode_pass(password)}\n")


def auth(player, name, password):
    return reply(player, "auth", f'NAME={name}\nPASS={encode_pass(password)}\nVERS="pc/1.4"\n')


def new(server):
    name, password, persona = KEEPER
    p = connect(server)
    expect(acct(p, "abc", password), "acctinam", "account name of 3 characters")
    expect(acct(p, "a" * 17, password), "acctinam", "account name of 17 characters")
    expect(acct(p, "ab!cd", password), "acctimst", "account name with a symbol")
    expect(acct(p, "ab_cd", password), "acctimst", "account name with _ (the game can't type it)")
    expect(acct(p, name, "abc"), "acctipas", "password of 3 characters")
    expect(acct(p, name, password), "acct", "sign up")
    expect(acct(p, name.upper(), password), "acctdupl", "same name in capitals")
    expect(acct(p, "a" * 16, "b" * 16), "acct", "account name and password of 16 characters")

    # captured from the real game (PC 1.4): 16 times "a"
    captured = "~aj%22F*'h;h|.obU$~-Gn*w1sTSO[Jw<"
    expect(reply(p, "acct", f"NAME=captured\nPASS={captured}\n"), "acct", "sign up with a captured PASS")
    c = connect(server)  # a connection logs in once
    expect(auth(c, "captured", "a" * 16), "auth", "it decodes to 16 times a")
    expect(auth(c, "captured", "a" * 16), "authlogn", "a second login on the same connection")
    c.sock.close()

    expect(auth(p, "nobody", password), "authimst", "unknown account")
    expect(auth(p, name, "wrong123"), "authpass", "wrong password")
    expect(auth(p, name, password), "auth", "right password")

    expect(reply(p, "cper", "PERS=ab\n"), "cperinam", "persona of 2 characters")
    expect(reply(p, "cper", f"PERS={'c' * 13}\n"), "cperinam", "persona of 13 characters")
    expect(reply(p, "cper", 'PERS="a b"\n'), "cperiper", "persona with a space")
    expect(reply(p, "cper", f"PERS={persona}\n"), "cper", "persona")
    for i in range(3):
        expect(reply(p, "cper", f"PERS={persona}{i}\n"), "cper", f"persona {i + 2}")
    expect(reply(p, "cper", "PERS=fifth\n"), "cpermaxp", "a fifth persona")
    expect(reply(p, "dper", f"PERS={persona}2\n"), "dper", "delete a persona")

    other = connect(server)
    expect(acct(other, "another", password), "acct", "second account")
    expect(auth(other, "another", password), "auth", "second login")
    expect(reply(other, "cper", f"PERS={persona.upper()}\n"), "cperdupl", "persona of someone else")

    p.sock.close()
    other.sock.close()

    # five wrong passwords lock the account, but not for the address of its last login
    p = connect(server, "127.0.0.2")
    for i in range(5):
        expect(auth(p, name, "wrong123"), "authpass", f"wrong password {i + 1} from another address")
    expect(auth(p, name, password), "authlock", "right password once the account is locked")
    p = connect(server)
    expect(auth(p, name, password), "auth", "right password from the address of the last login")
    p.sock.close()

    # twenty failed logins block an address, whatever the account
    p = connect(server, "127.0.0.3")
    for i in range(20):
        if auth(p, f"nobody{i}", password) != "authimst":
            sys.exit(f"unknown account {i + 1} from one address: not authimst")
    expect(auth(p, "another", password), "authmany", "right password after 20 failed logins from the address")


def restarted(server):
    name, password, persona = KEEPER
    p = connect(server)
    expect(auth(p, name, "wrong123"), "authpass", "wrong password after the restart")
    fields = p.request("auth", f'NAME={name}\nPASS={encode_pass(password)}\nVERS="pc/1.4"\n', "auth")
    expect(fields["PERSONAS"], f"{persona},{persona}0,{persona}1", "personas after the restart")


def rusers(path):
    """Records as the old server wrote them on x86_64: Username[16],
    Personas[4][16] and an 8 byte pointer; the last one is the blank record
    every start of the old server added."""
    name, personas = OLD
    with open(path, "wb") as f:
        for n, ps in ((name, personas), ("", [])):
            fields = [n] + ps + [""] * (4 - len(ps))
            f.write(b"".join(struct.pack("16s", x.encode()) for x in fields) + bytes(8))


def imported(server):
    name, personas = OLD
    p = connect(server)
    expect(auth(p, name, "abc"), "authipas", "imported account, password too short")
    fields = p.request("auth", f'NAME={name}\nPASS={encode_pass("first123")}\nVERS="pc/1.4"\n', "auth")
    expect(fields["PERSONAS"], ",".join(personas), "imported account, first login sets the password")
    p = connect(server)
    expect(auth(p, name, "other123"), "authpass", "imported account, another password")
    expect(auth(p, name, "first123"), "auth", "imported account, the first password")


if __name__ == "__main__":
    target, step = sys.argv[1:3]
    {"new": new, "restarted": restarted, "rusers": rusers, "imported": imported}[step](target)
