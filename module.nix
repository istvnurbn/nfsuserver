# NixOS module for the Need for Speed: Underground lobby server.
#
#   imports = [ ./module.nix ];
#
#   services.nfsuserver = {
#     enable = true;
#     openFirewall = true;
#     settings.servername = "My NFSU server";
#   };
{
  config,
  lib,
  pkgs,
  ...
}:

let
  inherit (lib) mkOption types;

  cfg = config.services.nfsuserver;

  tcpPorts = [
    10900 # redirector: first contact of a game that is pointed straight at this server
    10901 # lobby: where a redirector (this one, or the one in the launcher) sends the game
    10800 # server info: without it the launcher shows the server as offline, and the public list rejects it
    10980 # statistics, read by the scripts in web/
  ];
  udpPorts = [
    10800 # LAN discovery by the launcher (answered to port 10801 of the sender)
  ];

  roomKeys = [
    "rooms_ranked_circuit"
    "rooms_ranked_sprint"
    "rooms_ranked_drift"
    "rooms_ranked_drag"
    "rooms_unranked_circuit"
    "rooms_unranked_sprint"
    "rooms_unranked_drift"
    "rooms_unranked_drag"
  ];

  # The config parser of the server is fragile: a key that is missing leaves
  # its variable uninitialised, and matching a key depends on the lines in
  # front of it. So nfsu.conf is always written out completely, in the order
  # of the sample file shipped with the server.
  settingsOrder = [
    "servername"
    "serverip"
    "serverexternalip"
    "welcomemessage"
    "enablelogfile"
    "rewritelogfile"
    "registerglobal"
    "disabletimestamp"
    "verbose"
    "logalltraffic"
    "banv1"
    "banv2"
    "banv3"
    "banv4"
    "banroomscreation"
    "bancheater"
  ]
  ++ roomKeys;

  render =
    value:
    if lib.isBool value then
      (if value then "1" else "0")
    else if lib.isList value then
      lib.concatStringsSep "," value
    else
      value;

  confLine = key: "${key} = ${render cfg.settings.${key}}";

  configFile = pkgs.writeText "nfsu.conf" (
    lib.concatMapStrings (key: confLine key + "\n") settingsOrder
  );
  newsFile = pkgs.writeText "news.txt" cfg.news;

  mkBool =
    default: description:
    mkOption {
      type = types.bool;
      inherit default description;
    };
in
{
  options.services.nfsuserver = {
    enable = lib.mkEnableOption "the Need for Speed: Underground lobby server";

    package = mkOption {
      type = types.package;
      default = pkgs.callPackage ./package.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage ./package.nix { }";
      description = "The nfsuserver package to use.";
    };

    openFirewall = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Open the ports of the server in the firewall: TCP 10900, 10901, 10800
        and 10980, and UDP 10800.
      '';
    };

    news = mkOption {
      type = types.lines;
      default = "Welcome to ${cfg.settings.servername}!\n";
      defaultText = lib.literalExpression ''"Welcome to ''${config.services.nfsuserver.settings.servername}!\n"'';
      description = "Text of the news screen in the game (`news.txt`), at most 10 kB.";
    };

    settings = mkOption {
      default = { };
      example = lib.literalExpression ''
        {
          servername = "My NFSU server";
          serverexternalip = "203.0.113.7";
          rooms_ranked_circuit = [ "Global" "Europe" ];
        }
      '';
      description = ''
        Contents of `nfsu.conf`. The option names are the keys of that file.
      '';
      type = types.submodule {
        options = {
          servername = mkOption {
            type = types.singleLineStr;
            default = "Another cool server";
            description = "Server name that players see in the launcher.";
          };

          serverip = mkOption {
            type = types.strMatching "([0-9]{1,3}\\.){3}[0-9]{1,3}";
            default = "0.0.0.0";
            description = ''
              IPv4 address to listen on, `0.0.0.0` for all interfaces. The
              server does not support IPv6.
            '';
          };

          serverexternalip = mkOption {
            type = types.singleLineStr;
            default = "";
            example = "203.0.113.7";
            description = ''
              Public IPv4 address of the server. Set it whenever the server
              sits behind NAT: a port forward on a home router, or a cloud
              provider that maps a public address to a private one. It is used
              for two things.

              A game that is pointed straight at this server (a hosts entry
              for `ps2nfs04.ea.com` instead of the launcher) first asks the
              redirector on port 10900 where the lobby is, and is sent to this
              address. Left empty, the answer is the local address that
              connection arrived on, which is only right if the public address
              is configured on an interface of this host. A game on the
              machine of the server is always sent to the address it used. A
              game on another machine in the LAN is sent to the public address
              like everybody else and depends on the router letting it back in
              (NAT loopback); the launcher avoids that, it does the
              redirecting itself.

              The games race each other directly, and the server tells them
              where the others are. Players on the machine or in the private
              network of the server are announced to players outside with this
              address, instead of one like `127.0.0.1` or `192.168.1.10` that
              is of no use there. Among themselves they get to know each other
              by their private addresses.
            '';
          };

          welcomemessage = mkOption {
            type = types.singleLineStr;
            default = "";
            description = "Message a player receives when joining a room, empty for none.";
          };

          enablelogfile = mkBool false ''
            Also write the log to `server.log` in {file}`/var/lib/nfsuserver`.
            Off by default (unlike the sample `nfsu.conf`) because the log
            goes to the journal anyway.
          '';

          rewritelogfile = mkBool true "Truncate `server.log` on every start instead of appending to it.";

          registerglobal = mkBool false ''
            Announce the server to the public server list of the launcher
            (the tracker at `nfsug.harpywar.com`) once a minute. The tracker
            only lists servers it can reach on TCP port 10800.
          '';

          disabletimestamp = mkBool true ''
            Do not prefix log lines with the time. On by default (unlike the
            sample `nfsu.conf`) because the journal records it already.
          '';

          verbose = mkBool false ''
            Debug logging. Not meant for a public server: every message is
            logged, and a crafted one overflows the log buffer, which
            terminates the server.
          '';

          logalltraffic = mkBool false ''
            Dump all lobby traffic to `traffic.log` in
            {file}`/var/lib/nfsuserver`. The file is never rotated. Off by
            default (unlike the sample `nfsu.conf`).
          '';

          banroomscreation = mkBool false "Do not let players create their own rooms.";

          bancheater = mkBool false "Disconnect players whose car was tuned with a cheat tool.";
        }
        // lib.genAttrs [ "banv1" "banv2" "banv3" "banv4" ] (
          key:
          mkBool false ''
            Reject players whose game is version 1.${lib.removePrefix "banv" key}.
            Different game versions cannot race each other.
          ''
        )
        // lib.genAttrs roomKeys (
          key:
          mkOption {
            # "A." .. "H." is put in front of the name, 29 characters are shown.
            type = types.nonEmptyListOf (types.strMatching "[^,\n]{1,27}");
            default = [ "Global" ];
            description = ''
              Permanent rooms for ${
                lib.replaceStrings [ "_" ] [ " " ] (lib.removePrefix "rooms_" key)
              } races. All names of one list together, including the commas
              between them, must not be longer than 63 characters.
            '';
          }
        );
      };
    };
  };

  config = lib.mkIf cfg.enable {
    assertions =
      map (key: {
        assertion = lib.stringLength (confLine key) <= 100;
        message = ''
          services.nfsuserver.settings.${key}: the resulting nfsu.conf line is longer
          than 100 characters, which the config parser of the server cannot handle.
        '';
      }) settingsOrder
      ++ map (key: {
        assertion = lib.stringLength (render cfg.settings.${key}) <= 63;
        message = ''
          services.nfsuserver.settings.${key}: the room names, joined with commas, are
          longer than 63 characters. The server reads them into a 64 byte buffer.
        '';
      }) roomKeys;

    networking.firewall = lib.mkIf cfg.openFirewall {
      allowedTCPPorts = tcpPorts;
      allowedUDPPorts = udpPorts;
    };

    systemd.services.nfsuserver = {
      description = "Need for Speed: Underground lobby server";
      wantedBy = [ "multi-user.target" ];
      wants = [ "network-online.target" ];
      after = [ "network-online.target" ];

      # The server only knows relative paths. It reads nfsu.conf and news.txt
      # from its working directory and keeps its databases (rusers.dat,
      # stat.dat, s*.dat) and optional log files there as well.
      preStart = ''
        ln -sfn ${configFile} nfsu.conf
        ln -sfn ${newsFile} news.txt
      '';

      serviceConfig = {
        ExecStart = lib.getExe cfg.package;
        WorkingDirectory = "%S/nfsuserver";
        StateDirectory = "nfsuserver";
        StateDirectoryMode = "0700";
        UMask = "0077";
        DynamicUser = true;

        # It has no way to recover from errors, and input it does not expect
        # tends to crash it. Accounts and statistics are written to disk as
        # they change, so bringing it back up loses only the open rooms.
        Restart = "always";
        RestartSec = 5;

        # select() based: a descriptor above 1023 would overflow the fd_set.
        LimitNOFILE = 1024;

        # 20 year old C++ that parses whatever the internet sends it with
        # sprintf and strcpy. All it needs is its state directory and IPv4.
        CapabilityBoundingSet = "";
        LockPersonality = true;
        MemoryDenyWriteExecute = true;
        NoNewPrivileges = true;
        PrivateDevices = true;
        PrivateTmp = true;
        PrivateUsers = true;
        ProcSubset = "pid";
        ProtectClock = true;
        ProtectControlGroups = true;
        ProtectHome = true;
        ProtectHostname = true;
        ProtectKernelLogs = true;
        ProtectKernelModules = true;
        ProtectKernelTunables = true;
        ProtectProc = "invisible";
        ProtectSystem = "strict";
        RemoveIPC = true;
        # AF_UNIX and AF_INET6 are for name resolution (registerglobal).
        RestrictAddressFamilies = [
          "AF_INET"
          "AF_INET6"
          "AF_UNIX"
        ];
        RestrictNamespaces = true;
        RestrictRealtime = true;
        RestrictSUIDSGID = true;
        SystemCallArchitectures = "native";
        SystemCallFilter = [
          "@system-service"
          "~@privileged"
          "~@resources"
        ];
      };
    };
  };
}
