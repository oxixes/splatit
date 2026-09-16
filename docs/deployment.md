# Deployment

## Ports

Plan the firewall before anything else. The console talks to four of these
directly, and the rest stay internal.

| Port | Protocol | Subsystem | Who connects |
| --- | --- | --- | --- |
| 443 | TCP | Account and BOSS | The console |
| 1201 | UDP | Friends auth | The console |
| 1202 | UDP | Friends secure | The console |
| 1203 | UDP | Splatoon auth | The console |
| 1204 | UDP | Splatoon secure | The console |
| 1999 | TCP | gRPC control plane | Other nodes only |
| 3000 | TCP | Management API | Operators only |
| 8080 | TCP | Admin panel | Operators only |

Never expose 1999 to the internet: the gRPC plane has no authentication beyond
optional mutual TLS. Port 3000 needs its own TLS turned on before it leaves a
trusted network, since it defaults to plain HTTP.

## Docker image

The `Dockerfile` builds in two stages. The first pulls the exact vcpkg commit
that `vcpkg.json` pins, compiles every dependency and then the server. The second
copies the binary and the runtime asset files into a slim Debian image with
`ca-certificates` and `tzdata` installed.

```bash
git submodule update --init --recursive && docker build -t splatit/server:latest .
```

The build takes 45 to 90 minutes the first time and needs roughly 15 GB of free
space. A BuildKit cache mount keeps the vcpkg downloads between builds, so
rebuilding after a code change takes a couple of minutes.

Two details in the image are worth knowing about:

- **The image installs `tzdata` on purpose.** On Linux the `date` library reads the
  operating system time zone database, so removing the package breaks the
  account server. The manual download described in [Building](building.md) only
  applies to Windows.
- **The entrypoint creates `certs/`, `miis/` and `boss/`** inside the data
  directory before starting the server, because the certificate manager expects
  those directories to exist already.

The image runs as UID 10001 rather than root. Binding to port 443 still works on
a normal bridge network, since Docker lowers the unprivileged port floor inside
the container. With `network_mode: host` that floor comes from the host instead,
so either raise `http.listenPort` above 1024 and proxy to it, or set
`net.ipv4.ip_unprivileged_port_start=0` on the host.

## Single node with Compose

The quickest working deployment: one server process running everything, SQLite
files, in-process shared state, and the admin panel next to it.

### 1. Prepare the data directory

```bash
mkdir -p data/certs data/miis
cp docker/settings/single-node.example.json data/settings.json
```

### 2. Generate the keys

```bash
openssl rand -base64 32
```

Run it five times and paste the results into `accounts.tokenKey`,
`accounts.refreshTokenKey`, `nex.tokenKey`, `friendsSecure.serverKey` and
`splatoonSecure.serverKey`.

### 3. Set the address the console reaches

Replace every `192.168.1.10` in `data/settings.json` with the LAN address of the
Docker host. Those values reach the console verbatim, so a loopback or container
address there produces a game that connects to the account server and then times
out on matchmaking.

The keys that matter are `accounts.hosts[*][*].address`,
`friendsAuth.secure.address` and `splatoonAuth.secure.address`.

### 4. Point the panel at the API

`docker/web-config.json` holds the admin panel configuration. Change `apiUrl`
when the panel runs anywhere other than the operator's own machine:

```json
{
  "apiUrl": "http://192.168.1.10:3000",
  "compatibleVersions": [1]
}
```

Set `management.corsOrigin` in `data/settings.json` to the panel origin, for
example `http://192.168.1.10:8080`.

### 5. Start it

```bash
docker compose up -d --build
```

```bash
docker compose logs -f server
```

The first start generates the CA, the server certificate and the device key
under `data/certs/`, runs every migration and creates the default administrator.

The admin panel answers on `http://localhost:8080` and the management API on
port 3000.

```{admonition} Change the default administrator
:class: danger

The initial migration creates an account named `admin` with the password
`splatit` and PID 1799999999. Change that password through the admin panel
before the server touches an untrusted network.
```

## Distributed with Compose

`docker-compose.distributed.yml` splits the work across a gateway and two game
nodes, backed by PostgreSQL and Redis.

```{mermaid}
flowchart TB
    console["Console or Cemu"]

    subgraph host["Docker host"]
        gw["gateway<br/>accounts, BOSS, management<br/>TCP 443, 3000"]
        na["node-a<br/>auth + secure pair<br/>UDP 1201-1204"]
        nb["node-b<br/>auth + secure pair<br/>UDP 1211-1214"]
        pg[("postgres")]
        rd[("redis")]
        web["web 8080"]
    end

    console -->|HTTPS| gw
    console -->|PRUDP| na
    console -->|PRUDP| nb
    gw -->|gRPC| na
    gw -->|gRPC| nb
    na <-->|shared state| rd
    nb <-->|shared state| rd
    gw --> pg
    na --> pg
    nb --> pg
    web --> gw
```

The services find each other by service name over Docker's embedded DNS, so the
settings files name them directly. Only the addresses the console dials stay as
literal IPv4, since the console receives them inside a NEX token and cannot
resolve a name.

### 1. Lay out the data directories

```bash
mkdir -p data/gateway/certs data/gateway/miis data/node-a/certs data/node-b/certs
cp docker/settings/distributed-gateway.example.json data/gateway/settings.json
cp docker/settings/distributed-node-a.example.json  data/node-a/settings.json
cp docker/settings/distributed-node-b.example.json  data/node-b/settings.json
```

### 2. Fill in the placeholders

Every file contains `REPLACE_ME` and `REPLACE_WITH_...` markers. Four rules
govern the values:

- All three files share one `nex.tokenKey`. A mismatch makes the secure servers
  reject tickets that the auth servers issued.
- All nodes running the same secure service must share the same secure server key
  (`friendsSecure.serverKey` and `splatoonSecure.serverKey`). If an auth server runs
  without the secure server on the same node, set `secure.serverKey` in the auth block
  to the same key.
- Each file keeps a different `nex.serverId`. The examples use 0, 1 and 2.
- Each file keeps its own `grpc.publicFacingAddress`, matching the static
  address that Compose assigns it.

Put the PostgreSQL password in an `.env` file next to the Compose file:

```bash
echo "POSTGRES_PASSWORD=$(openssl rand -base64 24)" > .env
```

Then paste the same password into the seven `db.password` fields across the
three settings files.

### 3. Copy the certificates to the game nodes

The auth and secure servers do not serve HTTPS, but they still load the SSL
block. Copy the CA and server certificate the gateway generated into each node's
`certs/` directory after the gateway's first start, or generate them once and
distribute them.

### 4. Start it in order

Starting everything at once makes several processes race to run the same initial
migration. Bring the stack up in stages instead:

```bash
docker compose -f docker-compose.distributed.yml up -d postgres redis
```

```bash
docker compose -f docker-compose.distributed.yml up -d --build gateway
```

```bash
docker compose -f docker-compose.distributed.yml up -d node-a
```

```bash
docker compose -f docker-compose.distributed.yml up -d node-b web
```

Watch the gateway log until the migrations finish before starting a node, and
watch node-a finish before starting node-b, since the two share the auth and
secure databases.

### Adding a node

Copy the node-b settings file, give the new file its own `nex.serverId`, its own
static address, its own UDP ports and its own `publicFacingAddress`. Then add the
new auth server to `accounts.hosts` and to `management.servers` on the gateway
and restart the gateway. The account server starts spreading players over the
new node immediately.

## Bare metal

Docker is not a requirement. A systemd unit works just as well:

```ini
[Unit]
Description=SplatIt server
After=network-online.target postgresql.service redis.service
Wants=network-online.target

[Service]
Type=simple
User=splatit
Group=splatit
WorkingDirectory=/opt/splatit
ExecStart=/opt/splatit/splatoon_server --data /var/lib/splatit --log-level 1
Restart=on-failure
RestartSec=5s
AmbientCapabilities=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/splatit

[Install]
WantedBy=multi-user.target
```

Copy the binary and every runtime asset file into `/opt/splatit`, since the
server resolves them relative to its working directory. `CAP_NET_BIND_SERVICE`
covers port 443 without running as root.

## DNS

The console has the Nintendo domains baked in, so a working deployment
intercepts them. Point these names at the server:

| Name | Points to |
| --- | --- |
| `account.<domain>` | The account server |
| `mii-secure.account.<domain>` | The account server |
| `npts.app.<domain>` | The BOSS server |
| `npdi.cdn.<domain>` | The BOSS server |
| `nppl.app.<domain>` | The BOSS server |

With `domain` at its default, that means five names under `nintendo.net`. A
dnsmasq entry covers all of them at once:

```text
address=/nintendo.net/192.168.1.10
```

Redirecting the whole zone also captures the shop and update services, which the
server does not implement. The console handles that gracefully, but pointing only
the five names above keeps the failures cleaner.

Cemu takes a different path and reads the URLs out of `network_services.xml`, so
it needs no DNS changes at all. See [Connecting a client](connecting.md).

## Certificates

The certificate manager generates a CA and a `*.nintendo.net` server certificate
on the first start, then reuses them. A console rejects that certificate until
its CA is trusted, which is what the emulator file bundle handles by shipping the
CA in the `scerts` folder.

For a real console, either install the CA into its certificate store or patch the
game to skip verification. The repository also carries a development CA under
`certs/`, useful for local testing and unsuitable for anything else, since its
private key is public.

## TLS between nodes

Add a `grpc.tls` block to every node to encrypt and mutually authenticate the
control plane:

```json
"grpc": {
  "enabled": true,
  "listenAddress": "0.0.0.0",
  "port": 1999,
  "reflection": false,
  "publicFacingAddress": "node-a:1999",
  "tls": {
    "certPath": "/app/data/certs/grpc.crt",
    "keyPath": "/app/data/certs/grpc.key",
    "caCertPath": "/app/data/certs/grpc-ca.crt"
  }
}
```

Each node presents `certPath` both as its server certificate and as its client
certificate, and verifies peers against `caCertPath`. Roll it out everywhere at
once, because a plaintext client cannot reach a TLS listener.

Generating a small internal CA takes three commands:

```bash
openssl req -x509 -newkey rsa:4096 -nodes -keyout grpc-ca.key -out grpc-ca.crt -days 3650 -subj "/CN=SplatIt gRPC CA"
```

```bash
openssl req -newkey rsa:4096 -nodes -keyout grpc.key -out grpc.csr -subj "/CN=splatit-node"
```

```bash
openssl x509 -req -in grpc.csr -CA grpc-ca.crt -CAkey grpc-ca.key -CAcreateserial -out grpc.crt -days 825 -extfile <(printf "subjectAltName=DNS:node-a")
```

Repeat the last two commands per node, putting the name that other nodes dial
into the SAN. Use `DNS:` for a service name and `IP:` for a literal address.

## Reverse proxy

The management API can terminate TLS itself, so a proxy is optional there. It
still earns its place in front of the admin panel, which serves plain HTTP, and
for access control and a managed certificate across both. Caddy keeps it short:

```text
admin.example.org {
    handle /api/* {
        reverse_proxy 127.0.0.1:3000
    }
    handle {
        reverse_proxy 127.0.0.1:8080
    }
}
```

Serving both from one origin also removes the CORS problem, so
`management.corsOrigin` can then point at `https://admin.example.org`.

Leave the account and BOSS listener alone. The console pins expectations about
the TLS handshake, and putting a modern proxy in front of it usually breaks the
connection rather than helping.
