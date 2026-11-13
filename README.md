
# Need For Speed: Underground Server

This server was initially developed by [3 PriedeZ](http://3priedez.net)

It support only 1-st [Need for Speed: Underground](https://en.wikipedia.org/wiki/Need_for_Speed:_Underground), but not Underground 2.

## Server Installation

### Linux

Download static binary from [Releases](https://github.com/HarpyWar/nfsuserver/releases) or build it from sources:

```
apt-get install git make build-essential
git clone https://github.com/HarpyWar/nfsuserver.git
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
* The log is in the journal: `journalctl -u nfsuserver -f`. Accounts and statistics (`rusers.dat`, `stat.dat`, `s*.dat`) are kept in `/var/lib/nfsuserver`.
* The service is sandboxed and restarted automatically when it stops.
* `nix-build nixos-test.nix` boots the module in virtual machines and tests it.

To only build the server run `nix-build`. `result/bin/nfsuserver` has to be started in a directory that contains `nfsu.conf` and `news.txt`, samples are in `result/share/nfsuserver`.

### Windows

Download `nfsuserver.1.0.4.win32.zip` from [Releases](https://github.com/HarpyWar/nfsuserver/releases) or use Visual Studio to build own executable (uncomment [NT_SERVICE](https://github.com/HarpyWar/nfsuserver/blob/master/nfsuserver/win_nix.h#L5) flag for Windows service mode.). For compilation you have to install [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk/).

`nfsuserver.exe` is for normal start, not a service.

To install a server as a Windows Service run:
```
nfsuserver_svc.exe -i
```
To uninstall run:
```
nfsuserver_svc.exe -u
```




## Server Configuration

Edit `nfsu.conf` and `news.txt` to suit your needs.

After first start `server.log` should be created. After first user login a database `rusers.dat` with users should be created.

### Port forwarding

The server requires the following ports to be forwarded:

| Port | Used for |
| --- | --- |
| 10900 TCP | Redirector: first connection of a game that is pointed at the server with a `hosts` entry instead of the launcher |
| 10901 TCP | Lobby: accounts, rooms, chat and matchmaking |
| 10800 TCP | Server info for the launcher, which shows the server as offline without it, and for the public server list |
| 10800 UDP | `Find server` of the launcher, which only works within a local network |
| 10980 TCP | Statistics for the scripts in `web/` |

The race itself is played peer to peer between the games of the players and does not pass through the server. For that the player who hosts needs port 3658 UDP, see [Client Configuration](#client-configuration). It is not a port of the server.


## Client Configuration

1. Download and run [nfsuclient.exe](https://github.com/HarpyWar/nfsuserver/releases/download/client/nfsuclient.exe). Click on `Get Game` button to download ready to play NFSU game v1.4 with noCD and Widescreen with HD resolution.

2. Click `Get public serverlist` or `Add Server` to put your server address manually, and then click `Use Server`

3. Run the game &rarr; Select `Play Online` &rarr; Create a new profile, agree with rules and click `Use Profile`

After a login you have to select car and join a room to create/join a game. Note that hosting a lobby requires port 3658 UDP to be forwarded to your own computer: it is the port the games of the other players connect to.

![NFSU Lobby](http://i.imgur.com/ntGM3VF.jpg)

### Linux (Wine, Proton)

The launcher cannot redirect the game here. It writes the address of the server into the Windows `hosts` file inside the Wine prefix, but Wine does not read that file: names are resolved by the Linux system. So point `ps2nfs04.ea.com`, the name the game looks up, at the server in the `hosts` file of Linux:

```
# /etc/hosts
203.0.113.7 ps2nfs04.ea.com
```

On NixOS:

```nix
networking.hosts."203.0.113.7" = [ "ps2nfs04.ea.com" ];
```

Then start the game without the launcher and continue with step 3. The game now asks the server itself where the lobby is, so

* port 10900 TCP of the server has to be reachable, and
* a server behind NAT needs `serverexternalip` set in `nfsu.conf`.

To host races, port 3658 UDP has to be open in the firewall of your own machine as well, on NixOS with `networking.firewall.allowedUDPPorts = [ 3658 ];`.


## Documentation

* [NFSU Server Status](https://github.com/HarpyWar/nfsuserver/wiki/NFSU-Server-Status)

