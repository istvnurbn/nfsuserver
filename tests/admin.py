#!/usr/bin/env python3
"""Check the website's admin login and pages (JSON API, port 10990).

  admin.py SERVER PASSWORD SECRET
  admin.py SERVER PASSWORD --lan

PASSWORD is the admin password whose hash the server reads, SECRET its base32
TOTP secret; --lan checks a server without one (LAN mode). The requests come from the address the server trusts as its
proxy, so X-Real-IP picks the visitor's address.
"""

import base64
import hmac
import http.client
import json
import re
import struct
import sys
import time

from lobby_addresses import encode_pass

from accounts import acct, auth, connect, expect
from stats import player


def totp(secret, step):
    """RFC 6238 with SHA-1, as authenticator apps do it."""
    key = base64.b32decode(secret.upper() + "=" * (-len(secret) % 8))
    h = hmac.new(key, struct.pack(">Q", step), "sha1").digest()
    o = h[19] & 15
    return "%06d" % ((struct.unpack(">I", h[o:o + 4])[0] & 0x7FFFFFFF) % 1000000)


def call(server, method, path, body=None, cookie="", ip="198.51.100.1", ctype="application/json"):
    headers = {"X-Real-IP": ip}
    if cookie:
        headers["Cookie"] = cookie
    data = None
    if body is not None:
        data = body if isinstance(body, str) else json.dumps(body)
        headers["Content-Type"] = ctype
    c = http.client.HTTPConnection(server, 10990, timeout=5)
    c.request(method, path, data, headers)
    r = c.getresponse()
    out = r.status, r.getheader("Set-Cookie") or "", json.loads(r.read() or b"{}")
    c.close()
    return out


def can_log_in(server, account, password):
    return auth(connect(server), account, password) == "auth"


def lan(server, password):
    """LAN mode: the password alone, and only from private addresses."""
    status, _, body = call(server, "GET", "/api/admin/session")
    expect((status, body.get("totp")), (401, False), "the page learns that no code is needed")
    for ip in ("198.51.100.9", "172.32.0.1", "2001:db8::1", "::ffff:8.8.8.8"):
        expect(call(server, "POST", "/api/admin/login", {"password": password}, ip=ip)[0], 403,
               f"the right password from a public address ({ip})")
    expect(call(server, "POST", "/api/admin/login", {"password": "wrong"}, ip="10.0.0.5")[0], 401,
           "a wrong password from the LAN")
    for ip in ("192.168.1.20", "10.1.2.3", "172.20.0.1", "127.0.0.1", "::1", "fd00::5", "fe80::1", "::ffff:192.168.1.1"):
        status, cookie, _ = call(server, "POST", "/api/admin/login", {"password": password}, ip=ip)
        expect(status, 200, f"the password alone from {ip}")
    expect(("Secure" in cookie, "HttpOnly" in cookie), (False, True), "a cookie that works over plain HTTP")
    session = cookie.split(";")[0]
    expect(call(server, "GET", "/api/admin/session", cookie=session)[2], {"admin": True, "totp": False}, "the session")
    lines = "".join(call(server, "GET", "/api/admin/log", cookie=session)[2]["lines"])
    expect("Admin login refused from 198.51.100.9: LAN mode takes private addresses only." in lines, True,
           "the refusal in the log")


def main():
    if sys.argv[3] == "--lan":
        return lan(*sys.argv[1:3])
    server, password, secret = sys.argv[1:4]
    expect(totp("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", 1), "287082", "the test's own TOTP (RFC 6238 at 59 s)")

    expect(call(server, "GET", "/api/admin/log")[0], 401, "the log without logging in")
    expect(call(server, "POST", "/api/admin/login", {"password": "wrong", "code": "000000"})[0], 401, "a wrong password")
    expect(call(server, "POST", "/api/admin/login", {"password": password, "code": "000000"})[0], 429,
           "a second try right after from the same address")
    expect(call(server, "POST", "/api/admin/login", "password=x", ip="198.51.100.2",
                ctype="application/x-www-form-urlencoded")[0], 415, "a form instead of JSON")

    code = totp(secret, int(time.time()) // 30)
    status, cookie, _ = call(server, "POST", "/api/admin/login", {"password": password, "code": code}, ip="198.51.100.3")
    expect(status, 200, "the right password and code")
    expect(all(f in cookie for f in ("HttpOnly", "Secure", "SameSite=Strict", "Path=/api/admin")), True,
           "the cookie's flags")
    session = cookie.split(";")[0]
    expect(call(server, "POST", "/api/admin/login", {"password": password, "code": code}, ip="198.51.100.4")[0], 401,
           "the same code a second time")
    expect(call(server, "GET", "/api/admin/session", cookie=session)[0], 200, "the session")
    expect(call(server, "GET", "/api/admin/session", cookie="nfsu_admin=" + "0" * 64)[0], 401, "a made-up session")

    p = connect(server)
    expect(acct(p, "resetme", "old12345"), "acct", "a player signs up")
    status, _, body = call(server, "POST", "/api/admin/password", {"account": "resetme", "password": "new12345"}, session)
    expect((status, body), (200, {"account": "resetme"}), "a typed password")
    expect(can_log_in(server, "resetme", "new12345"), True, "the player logs in with it")
    status, _, body = call(server, "POST", "/api/admin/password", {"account": "ResetMe", "password": ""}, session)
    made = body.get("password", "")
    expect((status, bool(re.fullmatch(r"[A-Za-z0-9]{10}", made))), (200, True), "a generated password")
    expect(can_log_in(server, "resetme", made), True, "the player logs in with the generated one")
    expect(call(server, "POST", "/api/admin/password", {"account": "nobody9", "password": "abcd1234"}, session)[0],
           404, "an account that doesn't exist")
    expect(call(server, "POST", "/api/admin/password", {"account": "resetme", "password": "a!"}, session)[0],
           400, "a password the game can't type")
    expect(call(server, "POST", "/api/admin/password", {"account": "resetme"}, "nfsu_admin=x")[0], 401,
           "a reset without a session")

    status, _, body = call(server, "POST", "/api/admin/accounts", {"account": "adminmade", "password": "made1234"}, session)
    expect((status, body), (200, {"account": "adminmade"}), "adding a player with a typed password")
    expect(can_log_in(server, "adminmade", "made1234"), True, "the new player logs in")
    status, _, body = call(server, "POST", "/api/admin/accounts", {"account": "adminmade2", "password": ""}, session)
    made2 = body.get("password", "")
    expect((status, bool(re.fullmatch(r"[A-Za-z0-9]{10}", made2))), (200, True), "adding a player with a generated password")
    expect(can_log_in(server, "adminmade2", made2), True, "that player logs in")
    expect(call(server, "POST", "/api/admin/accounts", {"account": "AdminMade", "password": "x1234"}, session)[0], 409,
           "an account name that exists (in other case)")
    expect(call(server, "POST", "/api/admin/accounts", {"account": "abc", "password": "x1234"}, session)[0], 400,
           "an account name of 3 characters")
    expect(call(server, "POST", "/api/admin/accounts", {"account": "goodname", "password": "a!"}, session)[0], 400,
           "a password the game can't type")
    expect(call(server, "GET", "/api/admin/accounts", cookie="nfsu_admin=x")[0], 401, "the list without a session")
    accounts = {a["account"]: a for a in call(server, "GET", "/api/admin/accounts", cookie=session)[2]["accounts"]}
    expect(accounts["adminmade"]["personas"], [], "a new account has no personas")
    expect(accounts["apihost"]["personas"], ["apihost"], "an account with its persona")
    expect((accounts["adminmade"]["created"] != "", accounts["adminmade"]["last_login"] is not None), (True, True),
           "created and last login")
    expect("goodname" in accounts, False, "nothing added by the refused tries")

    # deleting a player who is online, with a persona on the rank list
    online = player(server, "delme")
    status, _, body = call(server, "POST", "/api/admin/delete", {"account": "DelMe"}, session)
    expect((status, body), (200, {"account": "delme"}), "deleting an account (as stored)")
    try:
        online.read(1)
        gone = False
    except SystemExit:
        gone = True
    expect(gone, True, "the player online is disconnected")
    names = [a["account"] for a in call(server, "GET", "/api/admin/accounts", cookie=session)[2]["accounts"]]
    expect("delme" in names, False, "the account gone from the list")
    board = call(server, "GET", "/api/leaderboard?mode=all")[2]["players"]
    expect("delme" in [p["persona"] for p in board], False, "its persona gone from the leaderboard")
    expect(call(server, "POST", "/api/admin/delete", {"account": "delme"}, session)[0], 404, "deleting it again")
    call(server, "POST", "/api/admin/accounts", {"account": "delme", "password": "again123"}, session)
    p = connect(server)
    fields = p.request("auth", f'NAME=delme\nPASS={encode_pass("again123")}\nVERS="pc/1.4"\n', "auth")
    expect(fields.get("PERSONAS", ""), "", "a new account of that name starts without the old personas")

    # an account named like another player's persona: the server looks each up by its own kind
    p1 = connect(server)
    expect(acct(p1, "clashone", "clash123"), "acct", "a player signs up")
    p1 = connect(server)
    expect(auth(p1, "clashone", "clash123"), "auth", "and logs in")
    p1.request("cper", "PERS=clashtwo\n", "cper")
    p1.request("pers", "PERS=clashtwo\n", "pers")
    p2 = connect(server)
    expect(acct(p2, "clashtwo", "clash123"), "acct", "an account named like that persona signs up")
    p2 = connect(server)
    expect(auth(p2, "clashtwo", "clash123"), "auth", "and logs in while the persona is online")
    p2.request("cper", "PERS=clashthree\n", "cper")
    p1.request("pers", "PERS=clashtwo\n", "pers")
    print("ok  both stay connected, each with their own personas")
    for name in ("clashone", "clashtwo"):
        call(server, "POST", "/api/admin/delete", {"account": name}, session)

    lines = "".join(call(server, "GET", "/api/admin/log", cookie=session)[2]["lines"])
    expect("Admin deleted account delme (personas: delme)" in lines, True, "the deletion in the log")
    expect("Admin added account adminmade" in lines, True, "the new account in the log")
    expect("made1234" in lines or made2 in lines, False, "new players' passwords kept out of the log")
    expect("Admin set a new password for account resetme" in lines, True, "the reset in the log")
    expect("Admin login failed from 198.51.100.1" in lines, True, "the visitor's address from X-Real-IP in the log")
    expect("new12345" in lines or made in lines, False, "passwords kept out of the log")
    expect("servername" in call(server, "GET", "/api/admin/config", cookie=session)[2]["text"], True, "nfsu.conf")

    expect(call(server, "POST", "/api/admin/logout", {}, session)[0], 200, "logging out")
    expect(call(server, "GET", "/api/admin/session", cookie=session)[0], 401, "the session after logging out")


if __name__ == "__main__":
    main()