# Configuration

## The settings file

Everything lives in one file, `settings.json`, inside the data directory. On the
first start the server notices the file is missing, generates a complete default
one with freshly minted random keys, and then loads it. Stopping the server at
that point and editing the generated file is the easiest way to start.

`settings.schema.json` in the repository root describes the whole format, and
the server validates against it on every start. A settings file that fails
validation stops the server with an error naming the offending field, so the
schema doubles as the reference for anything this page leaves out.

Point an editor at the schema to get completion and inline documentation:

```json
{
  "$schema": "../settings.schema.json",
  "domain": "nintendo.net"
}
```

```{admonition} Which addresses accept a name and which do not
:class: caution

gRPC endpoints take a host name, an IPv4 literal or a bracketed IPv6 literal,
with a port up to five digits. Docker service names work, and so do
`localhost:1999` and `node.example.com:1999`. That covers
`accounts.hosts[*].grpcAddress`, `accounts.secureServerGrpcAddresses` and every
list under `management.servers`.

Listen addresses stay dotted-quad IPv4, because the server binds them directly.
So do the two console-facing addresses, `accounts.hosts[*].address` and
`<subsystem>.secure.address`: those travel to the console inside a NEX token,
and the console cannot resolve a name.

Note that gRPC resolves names through its own resolver, which does real DNS. A
Docker service name resolves because Docker runs an embedded DNS server. A bare
Windows machine name, the kind the host resolves over NetBIOS, does not.
```

## Top level

| Key | Type | Meaning |
| --- | --- | --- |
| `domain` | string | The domain the game resolves. Everything else derives from it: `account.<domain>`, `mii-secure.account.<domain>`, `npts.app.<domain>`, `npdi.cdn.<domain>` and `nppl.app.<domain>`. Keep it at `nintendo.net` unless the client carries a patch. |
| `ssl` | object | Paths to the CA and server certificate used by the HTTPS listener. |
| `http` | object | The listener shared by the account and BOSS servers. |
| `nex` | object | Settings shared by every NEX server. |
| `grpc` | object | The control plane listener. |
| `sharedState` | object | Where live session state lives. |

### `ssl`

| Key | Required | Meaning |
| --- | --- | --- |
| `caCert` | yes | CA certificate path. The server creates one when the file is missing. |
| `caKey` | no | CA private key path. Needed to generate the CA and to sign the server certificate. |
| `cert` | yes | Server certificate path, issued for `*.nintendo.net`. |
| `key` | yes | Server private key path. |

The certificate manager generates whatever is missing on startup, but it never
creates directories. Make sure the parent directory exists first, otherwise the
server stops with a message about the path not being a directory.

### `http`

| Key | Default | Meaning |
| --- | --- | --- |
| `listenAddress` | required | Bind address, usually `0.0.0.0`. |
| `listenPort` | 443 with TLS, 80 without | Listening port. |
| `workerCount` | required | Worker threads handling account and BOSS requests. |
| `keepAliveTimeout` | required | Seconds an idle keep-alive connection survives. |
| `ssl` | required | Whether to wrap the listener in TLS. The console always expects TLS, so turn this off only behind a terminating proxy. |

### `nex`

| Key | Meaning |
| --- | --- |
| `tokenKey` | 32 random bytes, base64 encoded, used to sign NEX tokens. Every node in a deployment must share the same value, otherwise the secure servers reject tickets the auth servers issued. |
| `serverId` | Numeric identity of this node. Unique per node. Shared state and notification routing both key off it. |
| `knownProxies` | Addresses whose forwarded-for headers the server trusts. Leave it empty unless a proxy sits in front. |

### `grpc`

| Key | Meaning |
| --- | --- |
| `enabled` | Turns the control plane on. Every subsystem except a completely isolated BOSS server needs it. |
| `listenAddress`, `port` | Bind address and port. |
| `reflection` | Exposes service descriptors for tools like `grpcurl`. Useful while developing, better off in production. |
| `publicFacingAddress` | The address other nodes use to reach this one. This value goes into Redis, so a wrong value silently breaks cross-node notifications. |
| `tls` | Optional mutual TLS, described below. |

#### `grpc.tls`

| Key | Meaning |
| --- | --- |
| `certPath` | Certificate used both as this node's server certificate and as its client certificate when calling other nodes. |
| `keyPath` | Matching private key. |
| `caCertPath` | CA that signs the peers' certificates. |

Setting the block turns on mutual authentication for every gRPC connection in
and out of the process. All nodes have to agree, since a plaintext client cannot
talk to a TLS listener.

### `sharedState`

| Key | Meaning |
| --- | --- |
| `type` | `local` or `redis`. |
| `redis` | Required when `type` is `redis`. |

`redis` takes `host`, `port`, `password`, `database` (0 to 15),
`connectionTimeoutMs`, `commandTimeoutMs`, `useSSL`, `caCertPath`, `certPath`,
`keyPath`, `workerThreads` and `clientTTLSeconds`. The TTL controls how long a
registered client survives in Redis without a refresh, which is what cleans up
after a node that dies mid-session.

## Database blocks

Every subsystem carries the same `db` object.

For SQLite:

```json
{
  "type": "SQLite3",
  "path": "/app/data/accounts.db"
}
```

For PostgreSQL:

```json
{
  "type": "PostgreSQL",
  "host": "postgres",
  "port": 5432,
  "user": "splatit",
  "password": "...",
  "name": "accounts"
}
```

```{warning}
Give every subsystem its own database. The initial migrations create tables
named `db_info`, `settings`, `pending_tasks` and `game_server_access` in more
than one subsystem, so two subsystems sharing a database collide as soon as the
second one migrates.
```

Two nodes running the same subsystem may share one database. Start one of them
alone the first time so it runs the migration by itself, then start the rest.

## Subsystem blocks

Every subsystem takes `enabled`. Setting it to `false` allows the rest of the
block to be absent entirely.

### `accounts`

| Key | Meaning |
| --- | --- |
| `tokenKey` | 32 random bytes, base64 encoded, signing access tokens. The management API signs its own JWTs with this key too. |
| `refreshTokenKey` | 32 random bytes, base64 encoded, signing refresh tokens. |
| `deviceKeyPath` | Private key that signs generated device certificates. The server creates it when missing. |
| `miiImagesPath` | Directory holding rendered Mii images. Create it before the first start. |
| `allowRealWiiU` | Accept connections presenting a genuine console certificate. |
| `allowGeneratedWiiU` | Accept connections presenting a certificate this server generated. |
| `hosts` | Auth servers per game server ID. |
| `secureServerGrpcAddresses` | Secure server gRPC addresses per game server ID, used to clean up sessions. |
| `email` | SMTP settings for verification and password reset. |
| `grpcRequestTimeout` | Milliseconds before an outbound gRPC call gives up. |
| `grpcConnectionPoolMaxSize` | Channels kept per target address. |

`hosts` maps a game server ID to a list of auth servers. `00003200` is the
friends server and `10162B00` is Splatoon:

```json
"hosts": {
  "00003200": [
    { "address": "192.168.1.10:1201", "grpcAddress": "node-a:1999" },
    { "address": "192.168.1.10:1211", "grpcAddress": "node-b:1999" }
  ],
  "10162B00": [
    { "address": "192.168.1.10:1203", "grpcAddress": "node-a:1999" },
    { "address": "192.168.1.10:1213", "grpcAddress": "node-b:1999" }
  ]
}
```

`address` is what the account server hands to the console, so it has to be
reachable from the console's network. `grpcAddress` is how the account server
reaches that auth server, so it only has to be reachable from the account server.
Listing several entries spreads players across them by round robin, and a node
that fails to answer drops out of the rotation for that request.

`email` with `"enabled": false` marks every address as already verified and
sends nothing, which is what a private server usually wants. Turning it on
requires `host`, `port`, `authMethod`, `username`, `password`, `sender` and
`sendTimeout`.

### `friendsAuth` and `splatoonAuth`

| Key | Meaning |
| --- | --- |
| `listenAddress`, `port` | UDP bind address and port. |
| `workerCount` | Worker threads. |
| `secure.address`, `secure.port` | Where this auth server sends clients afterwards. The console connects here directly, so use a routable address. |
| `secure.serverKey` | Optional 32 random bytes, base64 encoded, used to encrypt Kerberos tickets for the secure server. If omitted, the key is taken from the corresponding secure server block (`friendsSecure.serverKey` or `splatoonSecure.serverKey`). Required in distributed setups where the auth server runs without the secure server on the same node. |
| `db` | Database holding issued credentials. |

### `friendsSecure` and `splatoonSecure`

| Key | Meaning |
| --- | --- |
| `listenAddress`, `port` | UDP bind address and port. |
| `serverKey` | 32 random bytes, base64 encoded. Secret key used by the secure server to decrypt Kerberos tickets and authenticate sessions. |
| `workerCount` | Worker threads. Raise this one first when matchmaking feels slow. |
| `grpcRequestTimeout` | Milliseconds before an outbound gRPC call gives up. |
| `grpcConnectionPoolMaxSize` | Channels kept per target address. |
| `db` | Database holding the social graph or the Splatfest scores. |

### `boss`

Only `enabled` and `db`. Everything else derives from `domain`.

### `management`

| Key | Meaning |
| --- | --- |
| `listenAddress`, `listenPort` | Bind address and port for the REST API. |
| `workerCount`, `keepAliveTimeout` | Worker threads and keep-alive timeout. |
| `ssl` | Optional TLS for the API. Omit the block to serve plain HTTP. |
| `corsOrigin` | Origin allowed to call the API from a browser. Set it to the admin panel origin rather than leaving it at `*`. |
| `grpcRequestTimeout`, `grpcConnectionPoolMaxSize` | Outbound gRPC tuning. |
| `db` | Database holding operator data and pending management tasks. |
| `servers` | gRPC addresses to poll for status, one list per subsystem. |

`servers` needs all six keys, `accounts`, `boss`, `friendsAuth`,
`splatoonAuth`, `friendsSecure` and `splatoonSecure`, each holding a list of
gRPC addresses. The health page shows one row per address.

#### `management.ssl`

| Key | Meaning |
| --- | --- |
| `enabled` | Serve the API over HTTPS. Required whenever the block is present. |
| `certPath` | PEM certificate the API presents. Required when enabled. |
| `keyPath` | PEM private key matching the certificate. Required when enabled. |

The server never generates this certificate, and it stops with an error when a
file is missing or the pair does not match rather than quietly serving plain
HTTP. [Managing a server](management.md) covers generating one.

## Generating keys

`tokenKey`, `refreshTokenKey`, `nex.tokenKey`, `friendsSecure.serverKey` and
`splatoonSecure.serverKey` all take 32 random bytes, base64 encoded:

```bash
openssl rand -base64 32
```

```powershell
[Convert]::ToBase64String((1..32 | ForEach-Object { Get-Random -Maximum 256 }))
```

Rotating `accounts.tokenKey` invalidates every access token and every management
session at once. Rotating `nex.tokenKey` invalidates NEX tokens, which drops
players mid-session, so change it while nobody is playing and change it on every
node at the same time. Rotating `friendsSecure.serverKey` or
`splatoonSecure.serverKey` invalidates active tickets, and all nodes running or
routing to that service must share the same key.

## A minimal single node file

```json
{
  "domain": "nintendo.net",
  "ssl": {
    "caCert": "/app/data/certs/ca.crt",
    "caKey": "/app/data/certs/ca.key",
    "cert": "/app/data/certs/any.nintendo.net.crt",
    "key": "/app/data/certs/any.nintendo.net.key"
  },
  "http": {
    "listenAddress": "0.0.0.0",
    "listenPort": 443,
    "workerCount": 3,
    "keepAliveTimeout": 10,
    "ssl": true
  },
  "nex": { "tokenKey": "...", "serverId": 0 },
  "grpc": {
    "enabled": true,
    "listenAddress": "0.0.0.0",
    "port": 1999,
    "reflection": false,
    "publicFacingAddress": "127.0.0.1:1999"
  },
  "sharedState": { "type": "local" },
  "accounts": { "enabled": true, "...": "see docker/settings/single-node.example.json" },
  "boss": { "enabled": true, "db": { "type": "SQLite3", "path": "/app/data/boss.db" } },
  "friendsAuth": { "enabled": true, "...": "..." },
  "friendsSecure": { "enabled": true, "...": "..." },
  "splatoonAuth": { "enabled": true, "...": "..." },
  "splatoonSecure": { "enabled": true, "...": "..." },
  "management": { "enabled": true, "...": "..." }
}
```

Three complete, schema-valid files ship with the repository:

- `docker/settings/single-node.example.json`
- `docker/settings/distributed-gateway.example.json`
- `docker/settings/distributed-node-a.example.json` and its `node-b` twin

## Data directory layout

```text
data/
    settings.json             the file described above
    certs/                    CA, server certificate, device key
    miis/                     rendered Mii images
    accounts.db               account server database
    boss.db                   BOSS server database
    friendsAuth.db            friends auth database
    friendsSecure.db          friends secure database
    splatoonAuth.db           Splatoon auth database
    splatoonSecure.db         Splatoon secure database
    management.db             management database
```

The server creates the data directory itself, but not `certs/` or `miis/`.
Create both before the first start.
