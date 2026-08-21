# SplatIt Server

A from-scratch reimplementation of the online services behind Splatoon for Wii U.
It speaks the Nintendo Network account protocol, the BOSS content delivery
protocol and the NEX game protocols, so an original console or an emulator can
sign in, add friends, matchmake and play again.

Everything lives in one C++20 executable. A single process can host every
subsystem on a laptop, or several processes can split the work across machines
and share their live state through Redis. A React admin panel sits on top of a
REST management API.

**Documentation:** [full docs](https://splatit.readthedocs.io/en/latest/).

## What it implements

| Subsystem | Replaces | Handles |
| --- | --- | --- |
| Account server | `account.nintendo.net` | Sign in, NNID accounts, Miis, device certificates, access tokens, NEX tokens |
| BOSS server | SpotPass content delivery | Stage rotation, Splatfest announcements, panel and team textures |
| Friends auth and secure | NEX friends servers | Friend lists, presence, requests, blocking, notifications |
| Splatoon auth and secure | NEX game servers | Lobbies, matchmaking, NAT traversal, Splatfest scoring |
| Management API | Nothing official | Operator REST API for accounts, devices, Splatfests and live state |
| Admin panel | Nothing official | React interface over the management API |
| gRPC control plane | Nothing official | How the subsystems reach each other, in-process or across nodes |

## Quick start with Docker

```bash
git clone https://github.com/oxixes/splatit.git && cd splatit && git submodule update --init --recursive
```

```bash
mkdir -p data/certs data/miis && cp docker/settings/single-node.example.json data/settings.json
```

Generate three keys with `openssl rand -base64 32` and paste them into
`accounts.tokenKey`, `accounts.refreshTokenKey` and `nex.tokenKey`. Then replace
every `192.168.1.10` in `data/settings.json` with the LAN address that the
console reaches this machine on.

```bash
docker compose up -d --build
```

The first build takes 45 to 90 minutes, because vcpkg compiles gRPC, Boost and
OpenSSL from source. Afterwards the admin panel answers on
`http://localhost:8080` and the management API on port 3000.

> **Change the default administrator.** The first migration creates an account
> named `admin` with the password `splatit`. Change it before the server touches
> an untrusted network.

`docker-compose.distributed.yml` covers the multi-node setup: a gateway node, two
game nodes, PostgreSQL and Redis. [Deployment](docs/deployment.md) walks through
both.

## Building from source

Requirements: CMake 3.22, vcpkg, about 15 GB of disk space for the dependency
build, and a compiler with `std::format`: MSVC 19.34, GCC 13 or Clang 17.

```bash
git submodule update --init --recursive
```

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

```bash
cmake --build build --parallel
```

### Windows needs the time zone database

The project uses Howard Hinnant's `date` library. On Windows, vcpkg builds it
with `USE_OS_TZDB=0` and `HAS_REMOTE_API=0`, so it neither reads the Windows
registry zones nor downloads anything itself. Nothing fails at build time. The
account server throws at run time instead, on the first request that touches a
user's local time.

Do this once:

1. Download the current IANA time zone database from
   <https://www.iana.org/time-zones> and extract `tzdataXXXXx.tar.gz` into
   `%USERPROFILE%\Downloads\tzdata`. That exact path is where the library looks.
2. Add `windowsZones.xml` to the same folder:

   ```powershell
   Invoke-WebRequest -Uri "https://raw.githubusercontent.com/unicode-org/cldr/main/common/supplemental/windowsZones.xml" -OutFile "$env:USERPROFILE\Downloads\tzdata\windowsZones.xml"
   ```

Linux needs none of this. There, vcpkg builds the library against the operating
system database, so installing the `tzdata` package is enough. The Docker image
already includes it.

Run the server from the repository root, since it resolves
`settings.schema.json`, `timezones.json` and the default images relative to the
working directory.

```bash
./build/splatoon_server --data ./data --log-level 1
```

### Admin panel

```bash
cd web && npm install && npm run build
```

That produces a static site in `web/build/client`. Serve it with any web
server, or use the `web` service in the Compose files, which puts nginx in
front of it.

## Repository layout

```text
src/                 the server
    http/            HTTP parser, virtual host router, account, BOSS, management
    nex/             PRUDP transport, RMC dispatch, auth, friends, Splatoon
    grpc/            control plane server and services
    db/              database abstraction, SQLite, PostgreSQL, migrations
    sharedState/     local and Redis shared session state
    boss/            BYAML and BFRES generation for SpotPass payloads
    crypto/          certificates, RC4, AES, HMAC, JWT
    util/            coroutines, schedulers, background jobs
proto/               gRPC service definitions
web/                 React Router admin panel
docker/              entrypoint, example settings, Postgres init
docs/                the documentation in this repository
lib/                 submodules: pugixml, json-schema-validator, stb
```

## Credits

This project would not exist without the people who reverse engineered these
protocols and wrote down what they found.

- **[kinnay's NintendoClients wiki](https://github.com/kinnay/NintendoClients/wiki)**
  is the source for how Nintendo Network works: PRUDP, RMC, the Kerberos ticket
  scheme, the NEX protocol and method numbering, and the account server API.
  Every protocol detail implemented here traces back to that wiki.
- **[Pretendo Network](https://pretendo.network/)** worked out the BOSS
  internals, including how SpotPass payloads are encrypted, signed and packaged.

## Legal

This project reimplements protocols. It contains no Nintendo code and no game
assets. Running it still requires a legitimate copy of the game, and the emulator
path requires files dumped from a console you own. Splatoon, Wii U and Nintendo
Network are trademarks of Nintendo, which has nothing to do with this project.
