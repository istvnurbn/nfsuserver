# NixOS VM test for module.nix:
#
#   nix-build nixos-test.nix
#
# Boots a server with the firewall opened, a server with the firewall left
# closed and a client, and plays the part of the game and of the launcher
# against them.
{
  pkgs ? import <nixpkgs> { },
}:

pkgs.testers.runNixOSTest {
  name = "nfsuserver";

  # socat plays the game, the launcher and the tracker
  defaults =
    { pkgs, ... }:
    {
      environment.systemPackages = [ pkgs.socat ];
    };

  nodes = {
    server =
      { nodes, ... }:
      {
        imports = [ ./module.nix ];

        services.nfsuserver = {
          enable = true;
          openFirewall = true;
          news = "news from the test\n";
          settings = {
            servername = "VM test server";
            serverexternalip = "203.0.113.7";
            welcomemessage = "100% welcome";
            registerglobal = true;
            banv1 = true;
            rooms_ranked_circuit = [
              "Global"
              "Europe"
            ];
            rooms_unranked_drag = [ "Quarter mile" ];
          };
        };

        # The client stands in for the public server list tracker.
        networking.extraHosts = "${nodes.client.networking.primaryIPAddress} nfsug.harpywar.com";
      };

    closed = {
      imports = [ ./module.nix ];
      services.nfsuserver.enable = true;
    };

    client =
      { pkgs, ... }:
      {
        networking.firewall.allowedTCPPorts = [ 80 ];
        networking.firewall.allowedUDPPorts = [ 10801 ];

        systemd.services.fake-tracker =
          let
            # log the request line, answer like a web server would
            answer = pkgs.writeShellScript "fake-tracker-answer" ''
              head -n 1 >> /tmp/tracker.log
              printf 'HTTP/1.0 200 OK\r\n\r\nok'
            '';
          in
          {
            wantedBy = [ "multi-user.target" ];
            serviceConfig.ExecStart = "${pkgs.socat}/bin/socat TCP4-LISTEN:80,reuseaddr,fork EXEC:${answer}";
          };
      };
  };

  testScript = ''
    tcp_ports = [10900, 10901, 10800, 10980]

    # What the game sends first: a 12 byte header, the command and the total
    # length. The server answers with "@dir" and where to find the lobby.
    ask_redirector = (
        "(printf '@dir\\0\\0\\0\\0\\0\\0\\0\\014'; sleep 1)"
        " | socat -t 2 - TCP:{}:10900 | tr '\\0\\t' ' \\n'"
    )

    start_all()

    with subtest("the service starts and its log reaches the journal"):
        server.wait_for_unit("nfsuserver.service")
        for port in tcp_ports:
            server.wait_for_open_port(port)
        server.wait_until_succeeds(
            "journalctl -u nfsuserver -o cat | grep -F 'Listening for ClientReporterTcp connections on 0.0.0.0:10800 TCP'"
        )

    with subtest("the generated nfsu.conf is read the way it was meant"):
        journal = server.succeed("journalctl -u nfsuserver -o cat")
        for line in [
            "ServerName        VM test server",
            "ServerIP          0.0.0.0",
            "ServerExternalIP  203.0.113.7",
            "WelcomeMessage    100% welcome",
            "EnableLogFile     0",
            "RewriteLogFile    1",
            "DisableTimeStamp  1",
            "Verbose           0",
            "RegisterGlobal    1",
            "LogAllTraffic     0",
            "BanV1             1",
            "BanV2             0",
            "BanV3             0",
            "BanV4             0",
            "BanRoomsCreation  0",
            "BanCheater        0",
            "\tA.Global",
            "\tA.Europe",
            "\tB.Global",
            "\tC.Global",
            "\tD.Global",
            "\tE.Global",
            "\tF.Global",
            "\tG.Global",
            "\tH.Quarter mile",
        ]:
            assert line + "\n" in journal, f"missing in the journal: {line!r}"
        assert "[warn]" not in journal and "[error]" not in journal, journal

    with subtest("state lives in /var/lib/nfsuserver"):
        server.succeed("test -L /var/lib/nfsuserver/nfsu.conf")
        server.succeed("grep -Fx 'news from the test' /var/lib/nfsuserver/news.txt")
        server.fail("test -e /var/lib/nfsuserver/server.log")
        server.fail("test -e /var/lib/nfsuserver/traffic.log")

    with subtest("openFirewall lets other machines in"):
        client.wait_for_unit("multi-user.target")
        for port in tcp_ports:
            client.succeed(f"socat -u /dev/null TCP:server:{port},connect-timeout=5")

    with subtest("without openFirewall the server runs but is unreachable"):
        closed.wait_for_unit("nfsuserver.service")
        for port in tcp_ports:
            closed.wait_for_open_port(port)
            client.fail(f"socat -u /dev/null TCP:closed:{port},connect-timeout=3")

    with subtest("the redirector hands out serverexternalip"):
        reply = client.succeed(ask_redirector.format("server"))
        assert "ADDR=203.0.113.7\n" in reply and "PORT=10901\n" in reply, reply

    with subtest("without serverexternalip it hands out the local address"):
        own_address = closed.succeed("ip -4 -o addr show dev eth1 | grep -oP '(?<=inet )[0-9.]+'").strip()
        reply = closed.succeed(ask_redirector.format(own_address))
        assert f"ADDR={own_address}\n" in reply, reply

    with subtest("the launcher gets its answers"):
        info = client.succeed("socat -u TCP:server:10800 -")
        assert info.endswith("|*nix|1.0.5|VM test server"), info
        stats = client.succeed("socat -u TCP:server:10980 -")
        assert "VM test server" in stats and "A.Europe|0|[]" in stats, stats
        # LAN discovery: a datagram to 10800 is answered to port 10801
        client.succeed("socat -u UDP-RECV:10801 CREATE:/tmp/discovery >&2 & sleep 1")
        client.succeed("printf x | socat -u - UDP-SENDTO:server:10800")
        client.wait_until_succeeds("grep -Fx 1.0.5 /tmp/discovery")

    with subtest("registerglobal works from inside the sandbox"):
        client.wait_until_succeeds("grep -F 'GET /tracker/submit.php' /tmp/tracker.log", timeout=120)

    with subtest("a restart does not trip over connections in TIME_WAIT"):
        server.succeed("ss -tan | grep -E 'TIME-WAIT.*:109(00|80)'")
        server.succeed("systemctl restart nfsuserver.service")
        server.wait_for_unit("nfsuserver.service")
        server.wait_for_open_port(10900)
        reply = client.succeed(ask_redirector.format("server"))
        assert "ADDR=203.0.113.7\n" in reply, reply
        server.fail("journalctl -u nfsuserver -o cat | grep -F 'Could not bind'")
        assert server.succeed("systemctl show -p NRestarts --value nfsuserver.service").strip() == "0"

    with subtest("a failed start is reported and retried"):
        server.succeed("systemctl stop nfsuserver.service")
        # something else occupies the lobby port
        server.succeed("systemd-run --unit=port-squatter socat TCP4-LISTEN:10901,reuseaddr,fork /dev/null")
        server.wait_for_open_port(10901)
        server.succeed("systemctl start nfsuserver.service")
        server.wait_until_succeeds(
            "journalctl -u nfsuserver -o cat | grep -F '[error] Could not bind socket to 0.0.0.0:10901.'",
            timeout=60,
        )
        server.wait_until_succeeds("journalctl -u nfsuserver | grep -F 'status=1/FAILURE'", timeout=60)
        # once the port is free again the next automatic restart succeeds
        server.succeed("systemctl stop port-squatter.service")
        server.wait_until_succeeds(
            "systemctl is-active nfsuserver.service && socat -u /dev/null TCP:127.0.0.1:10901,connect-timeout=2",
            timeout=60,
        )
        reply = client.succeed(ask_redirector.format("server"))
        assert "ADDR=203.0.113.7\n" in reply, reply

    server.log(server.succeed("systemd-analyze security nfsuserver.service | tail -n 1"))
  '';
}
