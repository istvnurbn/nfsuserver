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
    10901 # lobby: where a redirector (this one, or one in a launcher) sends the game
    10800 # server info: without it launchers show the server as offline, and the public list rejects it
    10980 # status (rooms and players), read by launchers such as NFSUServerChanger
  ];
  udpPorts = [
    10800 # LAN discovery by launchers (answered to port 10801 of the sender)
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
    "trackers"
    "apiip"
    "apiproxyip"
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

  # systemd hands the admin secrets to the service here (LoadCredential)
  credentials = "/run/credentials/nfsuserver.service";
  adminOn = cfg.adminPasswordFile != null;

  configFile = pkgs.writeText "nfsu.conf" (
    lib.concatMapStrings (key: confLine key + "\n") settingsOrder
    + "adminpasswordfile = ${lib.optionalString adminOn "${credentials}/admin-password"}\n"
    + "admintotpfile = ${lib.optionalString adminOn "${credentials}/admin-totp"}\n"
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

    adminPasswordFile = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/run/secrets/nfsu-admin-password";
      description = ''
        File with the crypt hash of the website's admin password, made with
        `mkpasswd -m yescrypt`. Together with
        {option}`services.nfsuserver.adminTotpFile` it turns the admin login
        of the JSON API on. The service reads it as a systemd credential, so it
        can stay readable by root only. Give it as a string: a Nix path would
        be copied into the world-readable store.
      '';
    };

    adminTotpFile = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/run/secrets/nfsu-admin-totp";
      description = ''
        File with the base32 secret of the admin's authenticator app (TOTP),
        for example from `head -c 20 /dev/urandom | base32`. To change phones,
        put a new secret in it and restart the service.
      '';
    };

    website = {
      enable = lib.mkEnableOption ''
        the website: nginx serves the pages in {file}`site/` and passes
        {file}`/api/` on to the server's JSON API, with TLS from Let's Encrypt
        (set {option}`security.acme.acceptTerms` and
        {option}`security.acme.defaults.email`)'';

      hostName = mkOption {
        type = types.str;
        example = "nfsu.example.com";
        description = "Domain name of the website.";
      };

      root = mkOption {
        type = types.path;
        default = ./site;
        defaultText = lib.literalExpression "./site";
        description = "Directory with the pages.";
      };
    };

    news = mkOption {
      type = types.lines;
      default = "Welcome to ${cfg.settings.servername}!\n";
      defaultText = lib.literalExpression ''"Welcome to ''${config.services.nfsuserver.settings.servername}!\n"'';
      description = "Text of the news screen in the game (`news.txt`), at most 1000 bytes.";
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
            description = "Server name that players see in server lists.";
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
              for `ps2nfs04.ea.com`, or NFSUServerChanger) first asks the
              redirector on port 10900 where the lobby is, and is sent to this
              address. Left empty, the answer is the local address that
              connection arrived on, which is only right if the public address
              is configured on an interface of this host. A game on the
              machine of the server is always sent to the address it used. A
              game on another machine in the LAN is sent to the public address
              like everybody else and depends on the router letting it back in
              (NAT loopback); a launcher that does the redirecting
              itself avoids that.

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
            Announce the server to the public server lists in
            {option}`services.nfsuserver.settings.trackers` once a minute.
          '';

          trackers = mkOption {
            type = types.listOf (types.strMatching "(http://)?[A-Za-z0-9.-]+/?");
            default = [ ];
            example = [
              "nfsug.harpywar.com"
              "http://nfs.onl"
            ];
            description = ''
              Host names of the public server lists (trackers) that
              {option}`services.nfsuserver.settings.registerglobal` announces
              to, at `http://<host>/tracker/submit.php`; an `http://` in front
              is allowed. Launchers such as
              NFSUServerChanger read these lists. A tracker only lists servers
              it can reach on TCP port 10800.
            '';
          };

          apiip = mkOption {
            type = types.strMatching "(([0-9]{1,3}\\.){3}[0-9]{1,3})?";
            default = "127.0.0.1";
            description = ''
              IPv4 address the JSON API for the website listens on, at TCP
              port 10990; empty turns it off. It serves the status, races,
              leaderboard and track records as public data over plain HTTP,
              so only a reverse proxy on this host should reach it.
              {option}`services.nfsuserver.openFirewall` does not open it.
            '';
          };

          apiproxyip = mkOption {
            type = types.strMatching "([0-9]{1,3}\\.){3}[0-9]{1,3}";
            default = "127.0.0.1";
            description = ''
              Address of the reverse proxy in front of the JSON API. Only its
              `X-Real-IP` header is believed, which the admin login needs to
              slow down guessing per visitor.
            '';
          };

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
              between them, have to fit on one nfsu.conf line of at most 100
              characters.
            '';
          }
        );
      };
    };
  };

  config = lib.mkIf cfg.enable {
    warnings = lib.optional (cfg.settings.registerglobal && cfg.settings.trackers == [ ]) ''
      services.nfsuserver.settings.registerglobal is on, but settings.trackers is
      empty, so the server is not announced anywhere.
    '';

    assertions =
      map (key: {
        assertion = lib.stringLength (confLine key) <= 100;
        message = ''
          services.nfsuserver.settings.${key}: the resulting nfsu.conf line is longer
          than 100 characters, which the config parser of the server cannot handle.
        '';
      }) settingsOrder
      ++ [
        {
          assertion = !cfg.website.enable || cfg.settings.apiip == "127.0.0.1" && cfg.settings.apiproxyip == "127.0.0.1";
          message = "services.nfsuserver.website: nginx reaches the JSON API on 127.0.0.1, so keep settings.apiip and settings.apiproxyip at 127.0.0.1.";
        }
        {
          assertion = (cfg.adminPasswordFile == null) == (cfg.adminTotpFile == null);
          message = "services.nfsuserver: set both adminPasswordFile and adminTotpFile, or neither.";
        }
        {
          assertion = lib.stringLength cfg.news <= 1000;
          message = ''
            services.nfsuserver.news is longer than 1000 bytes. The server sends the
            news in one message of at most 1024 bytes and cuts it off after 1000.
          '';
        }
      ];

    networking.firewall = lib.mkIf cfg.openFirewall {
      allowedTCPPorts = tcpPorts ++ lib.optionals cfg.website.enable [
        80
        443
      ];
      allowedUDPPorts = udpPorts;
    };

    services.nginx = lib.mkIf cfg.website.enable {
      enable = true;
      recommendedTlsSettings = lib.mkDefault true;
      # about 10 admin logins a minute per address, on top of the server's own slowdown
      appendHttpConfig = ''
        limit_req_zone $binary_remote_addr zone=nfsu_login:1m rate=10r/m;
      '';
      virtualHosts.${cfg.website.hostName} =
        let
          api = {
            proxyPass = "http://127.0.0.1:10990";
            # sets X-Real-IP, which the admin login's slowdown needs
            recommendedProxySettings = true;
          };
        in
        {
          enableACME = lib.mkDefault true;
          forceSSL = lib.mkDefault true;
          inherit (cfg.website) root;
          extraConfig = ''
            add_header Content-Security-Policy "default-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'" always;
            add_header X-Content-Type-Options nosniff always;
            add_header Referrer-Policy no-referrer always;
          '';
          locations."/api/" = api;
          locations."= /api/admin/login" = api // {
            extraConfig = "limit_req zone=nfsu_login burst=5 nodelay;";
          };
        };
    };

    systemd.services.nfsuserver = {
      description = "Need for Speed: Underground lobby server";
      wantedBy = [ "multi-user.target" ];
      wants = [ "network-online.target" ];
      after = [ "network-online.target" ];

      # The server only knows relative paths. It reads nfsu.conf and news.txt
      # from its working directory and keeps its database (nfsu.db) and
      # optional log files there as well.
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
        LoadCredential = lib.optionals adminOn [
          "admin-password:${cfg.adminPasswordFile}"
          "admin-totp:${cfg.adminTotpFile}"
        ];

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
