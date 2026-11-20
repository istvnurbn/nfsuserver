
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
| 10980 TCP | Statistics for the scripts in `web/` |

The race itself is played peer to peer between the games of the players and does not pass through the server. For that the player who hosts needs port 3658 UDP, see [Client Configuration](#client-configuration). It is not a port of the server.

### Players in the network of the server

The server tells every game where to find the others, and what it tells depends on who it is for:

* Players on the machine of the server or in its private network (a VPN counts as one) get to know each other by their private addresses.
* To players outside they are announced with `serverexternalip`. So set it whenever the server is behind a router.
* A player on the machine of the server is announced to the others in its network with the address they reach the server on, not with `127.0.0.1`.

For a race between players inside and outside, port 3658 UDP has to be forwarded to the machine of the player inside who hosts, not to the server. A router can do that for one machine only.


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

