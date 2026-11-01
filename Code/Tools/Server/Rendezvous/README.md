# Running a rendezvous server (`gzh-rendezvous`)

A rendezvous server helps players find each other over the internet: it tells them under which address they are seen
from outside, introduces joining players and hosts to each other (so that their routers open up), keeps a list of
public games and relays packets when there is no direct way. It is small, needs no database and no accounts, and any
community can run one; several servers know each other through their peer list.

GZH Neo does not run a public server for everyone. Run your own for yourself, your friends or your community and tell
the players its address.

## 1. What a server sees and what it does not

- **Sees:** the public addresses and ports of the players using it, room codes, names and maps of publicly listed
  games, size and time of relayed packets.
- **Does not see:** content. Joining, lobby and game are sealed end to end (X25519, XChaCha20-Poly1305), through the
  relay as well.
- **Cannot:** pose as a host (the invite code contains the fingerprint of the host key), put players into other
  games, inject commands.
- **Stores:** nothing on disk. Rooms and relays live in memory and expire (60 s and 120 s without traffic). Addresses
  are only logged with `log_addresses = yes`; the statistics line contains none.

Whoever runs a public server processes IP addresses; in the EU that is processing of personal data (legitimate
interest: arranging game connections, no storage).

## 2. Building

**Linux** (any distribution with g++ or clang++ supporting C++20):

    tools/rendezvous/build_linux.sh /usr/local/bin/gzh-rendezvous
    gzh-rendezvous --selftest

**Windows:** the target `gzh-rendezvous` in the normal CMake build (`build-x64`); `ctest` runs its self-test.

**Docker** (in the repository root):

    docker build -f tools/rendezvous/Dockerfile -t gzh-rendezvous .
    docker run -d --name rendezvous -p 8090:8090/udp --restart unless-stopped gzh-rendezvous

The program depends on nothing but the C++ runtime; it only uses engine building blocks without game data
(`Engine/Rendezvous*`, `Wire`, `RateLimiter`, Monocypher).

## 3. Configuration

A file with one `key = value` per line; a template with all defaults is `tools/rendezvous/rendezvous.conf`.

| Key | Default | Meaning |
|---|---|---|
| `port` | 8090 | UDP port |
| `bind` | `::` | all addresses (IPv6 and IPv4); IPv4 only if there is no IPv6 |
| `max_rooms` | 4096 | rooms at the same time |
| `rooms_per_address` | 4 | rooms per sender (IPv6: per /64) |
| `room_ttl_seconds` | 60 | expiry without refresh (hosts refresh every 15 s) |
| `relay` | yes | relay packets |
| `max_relays` | 512 | relay sessions at the same time (one per game) |
| `relay_kib_per_session` | 128 | budget per session and second |
| `relay_mib_total` | 16 | budget of all sessions per second |
| `relay_ttl_seconds` | 120 | expiry without traffic |
| `knocks_per_second` | 4 | join attempts per room and second passed on to the host |
| `packets_per_second`, `packet_burst` | 60, 120 | control messages per sender |
| `peer` | -- | another server this one names to the players (several allowed) |
| `log_addresses` | no | log addresses (for debugging only) |
| `stats_seconds` | 300 | statistics line, 0 = off |

Start it with `gzh-rendezvous --config /etc/gzh-rendezvous.conf`; single values also work as switches (`--port`,
`--bind`, `--peer`, `--no-relay`, `--log-addresses`, `--stats`), see `--help`.

**Bandwidth:** a two-player game through the relay needs roughly 5 to 20 KiB/s per direction, an eight-player game up
to about 100 KiB/s. Without the relay (`relay = no`) only control traffic remains (a few bytes per player and second),
but players behind strict NAT then cannot meet.

## 4. As a service (systemd)

    install -m 755 gzh-rendezvous /usr/local/bin/
    install -m 644 tools/rendezvous/rendezvous.conf /etc/gzh-rendezvous.conf
    install -m 644 tools/rendezvous/gzh-rendezvous.service /etc/systemd/system/
    systemctl daemon-reload && systemctl enable --now gzh-rendezvous
    journalctl -u gzh-rendezvous

The unit runs without privileges (`DynamicUser`), without write access and with IPv4/IPv6 sockets only. The firewall
must let UDP 8090 through (for example `ufw allow 8090/udp`).

## 5. Telling the players

- **Players** add the server to `<user folder>\Rendezvous.txt` (one line `host[:port]`) or start the game with
  `-treffpunkt host[:port]`. Invite codes name their server themselves (`K7Q2-DP941QG2@host`).
- **Shipped with a build:** servers listed in `Data/Network/Rendezvous.txt` go into every build made from that
  source.
- **Peers:** with `peer = other.server` a server names the other one to its players; the games remember servers found
  this way (at most 64, `<user folder>\RendezvousFound.txt`). Mutual entries make the network robust: if one server
  goes down, the players find the others.

## 6. Protection and limits

Answers to unknown senders are never larger than the request (no amplifier for attacks with forged sender addresses),
every sender has a packet budget, rooms and relays can only be changed with their secret, and every message passes a
strict reader (unit tests and fuzzing). This does not help against an attack that fills the server's own line; that
is for the hosting provider to handle.
