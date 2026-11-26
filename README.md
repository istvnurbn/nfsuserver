
# Need For Speed: Underground Server

This server was initially developed by [3 PriedeZ](http://3priedez.net)

It support only 1-st [Need for Speed: Underground](https://en.wikipedia.org/wiki/Need_for_Speed:_Underground), but not Underground 2.

This fork runs on Linux only; the Windows build and the Windows launcher of the original are gone.

## Server Installation

### Linux

Build it from source:

```
apt-get install git make build-essential libsqlite3-dev libcrypt-dev
git clone https://github.com/istvnurbn/nfsuserver.git
cd nfsuserver/nfsuserver
make
./nfsuserver
```

### NixOS

`package.nix` builds the server and `module.nix` runs it as a systemd service. Make the repository available to your system configuration, with flakes:

```nix
# flake.nix
inputs.nfsuserver = {
  url = "github:istvnurbn/nfsuserver";
  flake = false;
};

# and in the modules of the host:
"${inputs.nfsuserver}/module.nix"
```

or without flakes:

```nix
# configuration.nix
imports = [
  "${builtins.fetchTarball "https://github.com/istvnurbn/nfsuserver/archive/master.tar.gz"}/module.nix"
];
```

Then enable the service:

```nix
services.nfsuserver = {
  enable = true;
  openFirewall = true;
  news = "Welcome!";
  settings = {
    servername = "My NFSU server";
    # Behind NAT (home router, most cloud providers): the public IPv4 address.
    serverexternalip = "203.0.113.7";
  };
};
```

* `settings` takes the keys of `nfsu.conf`, the file is generated from it. Room lists are written as lists, for example `rooms_ranked_circuit = [ "Global" "Europe" ];`. Other than in the sample `nfsu.conf`, `enablelogfile` and `logalltraffic` are off and `disabletimestamp` is on by default, because the log goes to the journal.
* `openFirewall` opens the [ports of the server](#port-forwarding) in the firewall of the host. Behind a router they have to be forwarded as well.
* The log is in the journal: `journalctl -u nfsuserver -f`. Accounts and statistics (`nfsu.db`) are kept in `/var/lib/nfsuserver`.
* The service is sandboxed and restarted automatically when it stops.
* `nix-build nixos-test.nix` boots the module in virtual machines and tests it.

To only build the server run `nix-build`. `result/bin/nfsuserver` has to be started in a directory that contains `nfsu.conf` and `news.txt`, samples are in `result/share/nfsuserver`.

## Server Configuration

Edit `nfsu.conf` and `news.txt` to suit your needs.

After first start `server.log` and the database `nfsu.db` should be created.

### Accounts

Accounts, personas, stats and track records are kept in the SQLite database `nfsu.db`, passwords as yescrypt hashes. Each race report is saved as it arrives. The rules follow the game's own messages:

* account names: 4 to 16 characters
* persona names: 3 to 12 characters
* passwords: 4 to 16 letters or digits

Names may use only letters and digits (the game refuses other characters in account names) and are unique regardless of case.

Against password guessing:

* 5 wrong passwords within 15 minutes lock an account for 15 minutes, but not for the address it last logged in from, so nobody can lock a player out.
* 20 failed logins within 15 minutes, on any accounts, block an address for 15 minutes.
* The counts are kept in memory; a restart clears them.

**Coming from the original server:** on its first start the server imports the accounts and personas from a `rusers.dat` in its directory, then the stats from `stat.dat` and the track records from the `s*.dat` files; it leaves the files untouched. Stats of personas that no longer exist are dropped. The old `rusers.dat` stored no passwords: the first login to an imported account sets its password, so whoever logs in first with an old name claims it.

### Port forwarding

The server requires the following ports to be forwarded:

| Port | Used for |
| --- | --- |
| 10900 TCP | Redirector: the first connection of a game, which asks where the lobby is |
| 10901 TCP | Lobby: accounts, rooms, chat and matchmaking |
| 10800 TCP | Server info for server lists |
| 10800 UDP | Server discovery within a local network |
| 10980 TCP | Status (rooms and players) for launchers such as NFSUServerChanger |

Port 10990 TCP is the JSON API for the website (`apiip`, `127.0.0.1` by default). Don't forward it: a reverse proxy on the same host serves it with TLS.

The race itself is played peer to peer between the games of the players and does not pass through the server. For that the player who hosts needs port 3658 UDP, see [Client Configuration](#client-configuration). It is not a port of the server.

### Players in the network of the server

The server tells every game where to find the others, and what it tells depends on who it is for:

* Players on the machine of the server or in its private network (a VPN counts as one) get to know each other by their private addresses.
* To players outside they are announced with `serverexternalip`. So set it whenever the server is behind a router.
* A player on the machine of the server is announced to the others in its network with the address they reach the server on, not with `127.0.0.1`.

For a race between players inside and outside, port 3658 UDP has to be forwarded to the machine of the player inside who hosts, not to the server. A router can do that for one machine only.

### Admin login for the website

The admin page (`site/admin.html`) adds, lists, renames and deletes players, adds, renames and deletes their personas (up to 4, as in the game), sets their passwords, and shows the server log and `nfsu.conf`. It is off until the files exist (only the password file in LAN mode, below):

```sh
mkpasswd -m yescrypt > admin-password        # asks for the password; use 16 characters or more
head -c 20 /dev/urandom | base32 > admin-totp
qrencode -t ansiutf8 "otpauth://totp/NFSU:admin?issuer=NFSU&secret=$(cat admin-totp)"   # scan with the phone
```

Set `adminpasswordfile` and `admintotpfile` in `nfsu.conf` to them (on NixOS: `services.nfsuserver.adminPasswordFile` and `adminTotpFile`, as strings so they stay out of the Nix store). Logging in takes the password and the 6-digit code from the phone. Wrong tries slow down that visitor's address, doubling up to 15 minutes; the account never locks. A lost phone: write a new secret to `admin-totp`, restart the server and scan the new code.

**LAN mode.** Leave `admintotpfile` empty (on NixOS: `adminTotpFile` unset) and the login asks for the password alone. It then accepts logins only from private addresses (10.x, 172.16–31.x, 192.168.x, loopback, link-local, fc00::/7) and works over plain HTTP. Use it only where the server sees each visitor's real address: behind a proxy or a Docker port forward that shows every visitor as one private address, the internet would count as the LAN. The server log names the address of every login and every refusal.

### Running the website

**NixOS.** The module sets up nginx with a Let's Encrypt certificate:

```nix
services.nfsuserver = {
  enable = true;
  openFirewall = true;          # also opens 80 and 443 for the website
  website = {
    enable = true;
    hostName = "nfsu.example.com";
  };
  adminPasswordFile = "/run/secrets/nfsu-admin-password";
  adminTotpFile = "/run/secrets/nfsu-admin-totp";
};
security.acme = {
  acceptTerms = true;
  defaults.email = "you@example.com";
};
```

**Docker.** `compose.yaml` runs the server and Caddy, which serves `site/` with a Let's Encrypt certificate. They share a network of their own (`172.30.99.0/24`; change it in `compose.yaml` and `docker/nfsu.conf` if it clashes), on which only Caddy reaches the JSON API:

```sh
echo DOMAIN=nfsu.example.com > .env
# docker/nfsu.conf: set serverexternalip to the host's public IPv4 address (see below)
mkpasswd -m yescrypt > docker/admin-password            # see "Admin login for the website" above
head -c 20 /dev/urandom | base32 > docker/admin-totp
sudo chown 10990 docker/admin-password docker/admin-totp   # the server's user in the container
chmod 400 docker/admin-password docker/admin-totp
docker compose up -d --build
```

Set `serverexternalip` in `docker/nfsu.conf` before players connect. The game first asks the server where the lobby is, and without it the server answers with the address the connection arrived on, which in a container is the container's private address (`172.30.99.x`) that no player can reach.

Edit `docker/nfsu.conf` and `docker/news.txt` for the server's settings and news, then `docker compose restart nfsuserver`. Accounts and stats live in the `data` volume. Point the domain's DNS at the host and keep ports 80 and 443 open for the certificate.

**Another reverse proxy.** Serve `site/` as static files and pass `/api/` on to `127.0.0.1:10990`. The admin login's slowdown needs the visitor's address, so the proxy has to set the `X-Real-IP` header (nginx: `proxy_set_header X-Real-IP $remote_addr;`, Caddy: `header_up X-Real-IP {remote_host}`). The server believes it only from `apiproxyip` (`127.0.0.1` by default).


## Client Configuration

The game finds its server by looking up `ps2nfs04.ea.com`. Point that name at your server in one of two ways:

* **The `hosts` file.** Add a line with the address of the server:

  ```
  203.0.113.7 ps2nfs04.ea.com
  ```

  On Windows the file is `C:\Windows\System32\drivers\etc\hosts`, and editing it needs administrator rights. On Linux (Wine, Proton) it is `/etc/hosts` of the Linux system: Wine resolves names through Linux, not through the `hosts` file inside its prefix. On NixOS:

  ```nix
  networking.hosts."203.0.113.7" = [ "ps2nfs04.ea.com" ];
  ```

* **[NFSUServerChanger](https://github.com/Pelorojo/NFSUServerChanger).** A mod for the game that sets the server without the `hosts` file, on Windows and under Wine. Set its Host option to the name or address of the server.

Then run the game &rarr; select `Play Online` &rarr; create a new profile, agree with the rules and click `Use Profile`. After a login, select a car and join a room to create or join a game.

The game first asks the server where the lobby is, so

* port 10900 TCP of the server has to be reachable, and
* a server behind NAT needs `serverexternalip` set in `nfsu.conf`.

If the game runs on the machine of the server, use `127.0.0.1`. On another machine in the LAN of a server with `serverexternalip` set, the game is sent to that public address for the lobby and depends on the router letting it back in (NAT loopback).

Hosting a race needs port 3658 UDP forwarded to your own computer and open in its firewall (on NixOS `networking.firewall.allowedUDPPorts = [ 3658 ];`): it is the port the games of the other players connect to.

![NFSU Lobby](http://i.imgur.com/ntGM3VF.jpg)


## Documentation

* [NFSU Server Status](https://github.com/HarpyWar/nfsuserver/wiki/NFSU-Server-Status)

