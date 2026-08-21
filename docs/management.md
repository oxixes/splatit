# Managing a server

Two pieces make up the operator side: a REST API served by the management
subsystem, and a React admin panel that talks to it. The panel holds no state of
its own, so anything it does is also available over the API.

## The admin panel

Start the `web` service from either Compose file and open port 8080, or serve
`web/build/client` with any static web server. The panel loads `/config.json`
at start, reads `apiUrl` from it and sends every request there.

### Signing in

Sign in with an account flagged as an administrator. The API checks the
credentials through `AuthenticateManagementUser` on the account server and
returns a JWT signed with `accounts.tokenKey`, valid for eight hours.

```{figure} _static/img/login.png
:alt: The admin panel login screen
:width: 100%

Signing in to the admin panel.
```

```{admonition} The default administrator
:class: danger

The initial migration of the accounts database creates an account named `admin`
with the password `splatit` and PID 1799999999. Change the password before the
server reaches an untrusted network.
```

### Dashboard

The landing page shows active players, active lobbies and total accounts, each
one pulled live from the secure servers over gRPC.

```{figure} _static/img/dashboard.png
:alt: The admin panel dashboard
:width: 100%

Live counters on the dashboard.
```

### Server status

**Server status** polls every gRPC address listed under `management.servers` and
reports each one as online or offline, with the error message when a call fails.
This is the page to check first when players report trouble.

```{figure} _static/img/server-status.png
:alt: The server status page
:width: 100%

Per node health, one row per configured gRPC address.
```

### Players

**Players** lists accounts with search and pagination. From here an operator can
create an account, edit its profile, change its email, set its Mii, link and
unlink devices, add and remove agreements, delete the account, and generate the
emulator file bundle described in [Connecting a client](connecting.md).

```{figure} _static/img/players.png
:alt: The players list
:width: 100%

The account list with per account actions.
```

### Devices

**Devices** lists known consoles by device ID, with their serial number, region
and status. Operators can register a device by hand, edit it, deactivate it or
delete it.

```{figure} _static/img/devices.png
:alt: The devices page
:width: 100%

Registered devices and their status.
```

### Lobbies

**Lobbies** reads live matchmaking sessions straight from the Splatoon secure
servers. Each row expands into the gathering ID, the host, the game mode, the
stage and the participants.

```{figure} _static/img/lobbies.png
:alt: The lobbies page
:width: 100%

Live matchmaking sessions.
```

### Settings

**Settings** covers the parts of server behaviour that change day to day:

- **Security status.** Four switches, `allowAccountCreation`, `allowRealWiiU`,
  `allowGeneratedWiiU` and `maintenanceMode`. The account server enforces them
  immediately, so `maintenanceMode` closes the server to players without a
  restart.
- **Agreements.** Publish a new EULA or privacy policy version per country and
  language, with the title, body and button labels the console shows. Publishing
  a new version makes clients prompt for acceptance again.
- **Splatfests.** Create, edit, switch and delete Splatfests.
- **Map rotation.** Edit or randomise the stage and rule rotation.

```{figure} _static/img/settings-security.png
:alt: The security status settings
:width: 100%

Security status switches.
```

### Splatfest editor

The Splatfest editor covers everything the game shows: the two team names in
every supported language, the announcement dialogue the idols read out, the
start and end times, the panel texture and the two team body textures. Saving
queues a BOSS payload rebuild through the global task scheduler, so the change
reaches consoles on their next SpotPass download rather than instantly.

```{figure} _static/img/splatfest-editor.png
:alt: The Splatfest editor
:width: 100%

Editing a Splatfest.
```

Finished Splatfests show their results, with per team totals pulled from the
Splatoon secure servers.

### Map rotation

The rotation editor sets which stages and which rules appear in each phase.
Saving rebuilds the BOSS `VSSetting` payload. The global task scheduler also
rotates stages once a day on its own, so hand editing only matters for a
specific event.

```{figure} _static/img/map-rotation.png
:alt: The map rotation editor
:width: 100%

Editing the stage and rule rotation.
```

## The management API

Base URL: `http://<host>:3000`, or `https://` once TLS is on. Responses are
JSON. Errors carry a code and a message:

```json
{
  "error": {
    "code": 4010,
    "message": "Missing or invalid authorization token"
  }
}
```

| Code | Meaning |
| --- | --- |
| 4000 | Bad request |
| 4010 | Permission denied |
| 4040 | Not found |
| 4050 | Method not allowed |
| 4090 | Conflict |
| 5000 | Internal error |
| 5020 | Bad gateway, usually a subsystem that failed to answer over gRPC |

### Serving it over TLS

The management API listens on plain HTTP by default. Add an `ssl` block to the
`management` settings to serve HTTPS instead:

```json
"ssl": {
  "enabled": true,
  "certPath": "/app/data/certs/management.crt",
  "keyPath": "/app/data/certs/management.key"
}
```

Both files are PEM. Unlike the console facing certificate, the server never
generates this one: a certificate no browser trusts would look like working TLS
while offering none. Point it at a real certificate, or generate a self signed
one for a private network:

```bash
openssl req -x509 -newkey rsa:2048 -nodes -keyout management.key -out management.crt -days 365 -subj "/CN=admin.example.org" -addext "subjectAltName=DNS:admin.example.org"
```

The server refuses to start when either file is missing or when the certificate
and key do not match, rather than falling back to plain HTTP. Remember to change
`apiUrl` in the admin panel configuration to `https://` and to update
`management.corsOrigin` to match.

### Authentication

Every endpoint except `/api/v1/status` and `/api/v1/auth/login` needs an
administrator token:

```bash
curl -s -X POST http://localhost:3000/api/v1/auth/login -H 'Content-Type: application/json' -d '{"username":"admin","password":"splatit"}'
```

```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "pid": 1799999999,
  "username": "admin",
  "isAdmin": true,
  "expiresAt": 1771234567
}
```

Send it as a bearer token afterwards:

```bash
curl -s http://localhost:3000/api/v1/accounts -H "Authorization: Bearer $TOKEN"
```

### Endpoints

The API is REST shaped, so each resource supports the usual verbs rather than
needing a line per path. `GET` on a collection lists it and `POST` creates,
while `GET`, `PUT` and `DELETE` on `{id}` read, update and remove one entry.

| Resource | Path | Verbs |
| --- | --- | --- |
| Accounts | `/api/v1/accounts`, `/api/v1/accounts/{pid}` | `GET` `POST` `PUT` `DELETE` |
| Account email and Mii | `/api/v1/accounts/{pid}/email`, `/mii` | `PUT` |
| Account agreements | `/api/v1/accounts/{pid}/agreements` | `POST` `DELETE` |
| Account devices | `/api/v1/accounts/{pid}/devices/{deviceId}` | `POST` `DELETE` |
| Device attributes | `/api/v1/accounts/{pid}/devices/{deviceId}/attributes/{name}` | `GET` `PUT` `DELETE` |
| Devices | `/api/v1/devices`, `/api/v1/devices/{deviceId}` | `GET` `POST` `PUT` `DELETE` |
| Agreements | `/api/v1/agreements` | `GET` `POST` `DELETE` |
| Security status | `/api/v1/security-status` | `GET` `PUT` |
| Splatfests | `/api/v1/festivals`, `/api/v1/festivals/{id}` | `GET` `POST` `DELETE` |
| Map rotation | `/api/v1/map-rotation` | `GET` `PUT` |

A handful of endpoints fall outside that pattern:

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/status` | API version and liveness, the only unauthenticated route besides login |
| `GET` | `/api/v1/server-status` | Health of every configured node |
| `GET` | `/api/v1/accounts:by-username` | Look an account up by name instead of PID |
| `POST` | `/api/v1/accounts/{pid}/cemu-files` | Generate the emulator bundle, needs the account password |
| `POST` | `/api/v1/festivals/switch` | Make a Splatfest the active one |
| `POST` | `/api/v1/map-rotation/randomize` | Generate a random rotation |
| `GET` | `/api/v1/friends/client_count` | Clients connected to the friends servers |
| `GET` | `/api/v1/splatoon/client_count`, `/lobby_count`, `/lobbies` | Live player and lobby state |
| `GET` | `/api/v1/splatoon/festival_totals?festivalId=<id>` | Splatfest score totals |

### Scripted examples

Put the server into maintenance mode:

```bash
curl -s -X PUT http://localhost:3000/api/v1/security-status -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d '{"maintenanceMode": true}'
```

Close registration while leaving existing players alone:

```bash
curl -s -X PUT http://localhost:3000/api/v1/security-status -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d '{"allowAccountCreation": false}'
```

Count who is online right now:

```bash
curl -s http://localhost:3000/api/v1/splatoon/client_count -H "Authorization: Bearer $TOKEN"
```

## Talking to gRPC directly

Turning on `grpc.reflection` makes the control plane browsable, which helps when
debugging a node that the management API reports as offline:

```bash
grpcurl -plaintext localhost:1999 list
```

```bash
grpcurl -plaintext -d '{"serverType": 1}' localhost:1999 serverstatus.v1.ServerStatusService/GetServerStatus
```

Turn reflection back off in production. The gRPC plane carries account
management calls and has no authentication of its own beyond optional mutual
TLS, which is why it must never be exposed to the internet. The management API 
should be the only way to talk to the gRPC services from outside the server network.
