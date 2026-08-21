# How the services work

This section describes the systems Nintendo built, not the way this project
reimplements them. Anyone writing a client, a server or a packet dissector needs
the same picture: which service the console talks to, in what order, and what
sits inside each packet.

```{admonition} Where this knowledge comes from
:class: seealso

Almost everything documented here was reverse engineered by the community and
written up in [kinnay's NintendoClients wiki](https://github.com/kinnay/NintendoClients/wiki).
That wiki is the reference for PRUDP, RMC, the Kerberos scheme, the NEX protocol
and method numbering, and the Nintendo Network account API. The protocol names
and identifiers used below follow its conventions. Read it first if you intend
to work on any of this.
```

## The service landscape

A Wii U game that plays online talks to three unrelated families of service.
They share no transport, no authentication and no addressing scheme.

```{mermaid}
flowchart TB
    console["Wii U console"]

    subgraph acct["Nintendo Network account services, HTTPS"]
        act["account.nintendo.net<br/>NNID accounts, tokens"]
        mii["mii-secure.account.nintendo.net<br/>rendered Mii images"]
    end

    subgraph boss["BOSS / SpotPass, HTTPS"]
        npts["npts.app.nintendo.net<br/>task sheets"]
        npdi["npdi.cdn.nintendo.net<br/>file payloads"]
        nppl["nppl.app.nintendo.net<br/>policy list"]
    end

    subgraph nex["NEX game services, PRUDP over UDP"]
        fauth["Friends auth<br/>server ID 00003200"]
        fsec["Friends secure"]
        gauth["Game auth<br/>server ID 10162B00 for Splatoon"]
        gsec["Game secure"]
    end

    console --> acct
    console --> boss
    console --> nex
    act -.->|"issues the NEX token"| gauth
    fauth -.->|"hands over a ticket"| fsec
    gauth -.->|"hands over a ticket"| gsec
```

The account services are ordinary HTTPS with XML bodies. BOSS is HTTPS serving
signed and encrypted binary blobs. NEX is a custom reliable protocol on top of
UDP, and it is where the interesting engineering lives.

## Signing in

Nothing reaches a game server without passing through the account server first.
The chain has four steps, and each one produces a credential for the next.

```{mermaid}
sequenceDiagram
    autonumber
    participant C as Console
    participant A as Account server
    participant AU as NEX auth server
    participant S as NEX secure server

    Note over C,A: client certificate proves the console is genuine
    C->>A: POST /v1/api/oauth20/access_token/generate
    A-->>C: access token, refresh token
    C->>A: GET /v1/api/provider/nex_token/@me?game_server_id=...
    A-->>C: host, port, PID, NEX password, encrypted token

    C->>AU: PRUDP session, then LoginEx with the token
    AU-->>C: Kerberos ticket and the secure server address
    C->>S: PRUDP session, CONNECT carrying the ticket
    S-->>C: session established
```

**Device authentication.** Every request to the account server carries a client
certificate burned into the console, along with headers identifying the device
ID, serial number, region and firmware. The server checks those before it looks
at any user credential, which is what stops an arbitrary HTTP client from
talking to it.

**Account credentials.** An NNID has a numeric principal ID, the PID, which is
the identity every other service uses. The password never travels in the clear
in the form the user typed: the console sends a hash derived from the PID and
the password.

**Tokens.** The access token authorises further account server calls. A service
token authorises a non-NEX Nintendo service. A NEX token is the handoff into a
game: it names the auth server address and port, the PID, a per-session NEX
password and an encrypted blob the auth server validates.

**Game server IDs.** Each title has its own NEX server, identified by an eight
digit hex ID. The friends service is `00003200` and Splatoon is `10162B00`. The
console asks for a token per server ID, so a game that shows a friend list holds
two independent NEX sessions.

## PRUDP, the NEX transport

NEX does not use TCP. It runs PRUDP, a reliable protocol built on UDP that adds
sessions, ordering, retransmission, fragmentation and encryption. Two wire
formats exist. Version 0 uses a compact binary header, and version 1 prefixes
packets with a magic number and a longer header carrying explicit sizes and a
substream identifier.

### Addressing

A single UDP port multiplexes several logical channels. Every packet carries a
source and destination **virtual port**, each combining a 4 bit port number with
a 4 bit stream type. Game traffic uses stream type 10, the one the wiki calls
`RVSecure`. This is why one server socket can serve unrelated streams without
extra ports.

### Packet types

| Type | Name | Purpose |
| --- | --- | --- |
| 0 | `SYN` | Opens the handshake and exchanges connection signatures |
| 1 | `CONNECT` | Completes the handshake, carrying the Kerberos ticket on a secure server |
| 2 | `DATA` | Carries RMC payloads |
| 3 | `DISCONNECT` | Tears the session down |
| 4 | `PING` | Keeps an idle session alive and measures liveness |

Flags on each packet mark it as an acknowledgement, as reliable, as needing an
acknowledgement, or as carrying an explicit payload size.

### Session setup

```{mermaid}
sequenceDiagram
    autonumber
    participant C as Client
    participant S as Server

    C->>S: SYN
    S-->>C: SYN + ACK, carrying the server connection signature
    C->>S: CONNECT, carrying the client signature, session key and ticket
    S-->>C: CONNECT + ACK, carrying the response check value
    loop while the session lives
        C->>S: DATA, reliable and sequenced
        S-->>C: ACK
        S-->>C: PING when the link goes quiet
    end
    C->>S: DISCONNECT
```

The **connection signature** binds the session to the client address, so packets
arriving from a different endpoint cannot take over an established session.

### Reliability and encryption

Reliable packets carry an incrementing sequence ID per substream. The receiver
acknowledges each one, and the sender retransmits on a timer until an
acknowledgement arrives or a retry limit forces the session closed. Payloads
larger than the maximum packet size split into fragments that the receiver
reassembles before handing the result to the layer above.

Every packet also carries a checksum keyed on the title's **access key**, an
ASCII string unique to the game. Splatoon uses `6f599f81` and the friends
service uses `ridfebb9`. The access key is not a secret in any real sense, since
it sits in the game binary, but it does mean traffic for one title cannot be
replayed against another.

Auth server traffic is encrypted with RC4 under a well known key. Secure server
traffic is encrypted with the session key that the client proves it holds, which
it obtains from the ticket described next.

## The Kerberos scheme

NEX borrows the shape of Kerberos. The auth server acts as the ticket granting
service, and the secure server never sees the user's credentials.

Every principal, whether a user or a server, has a **user key** derived from its
password:

```text
key = password
repeat 65000 + (PID mod 1024) times:
    key = MD5(key)
```

The iteration count varies per PID, so the derivation cost cannot be amortised
across accounts with a single precomputed table.

When a client asks for a ticket, the auth server produces a blob containing:

- A **ticket** encrypted with the *target server's* user key. Inside sits an
  expiry timestamp, the requesting PID and a freshly generated **session key**.
  An HMAC-MD5 over that ciphertext, keyed with the same user key, is appended.
- A **request data** section encrypted with the *client's* user key, holding the
  same session key plus a check value the server echoes back.

The client cannot read the ticket, only forward it. The secure server decrypts
it with its own key, verifies the MAC, rejects anything past its expiry, and
recovers both the caller's PID and the session key. From that point the session
is encrypted under a key that only the two endpoints and the auth server ever
held.

## RMC, the call layer

Once a PRUDP session exists, both ends exchange **RMC** messages, the remote
call format NEX uses. A request names:

- a **protocol ID**, identifying a family of calls,
- a **method ID** within that protocol,
- a **call ID**, matching a response to its request,
- a serialised parameter list.

A response echoes the protocol, method and call ID, then either a success flag
and return values or an error code. Servers also send RMC messages that expect
no reply, which is how notifications reach a client.

Parameters use a compact binary serialisation with a defined set of primitives:
fixed width integers, length prefixed strings and buffers, lists, maps, a
`Result` type, `StationURL` strings describing an endpoint, `DateTime` values,
and `Structure` types that carry an explicit version so a newer server can add
fields without breaking older clients. `AnyDataHolder` wraps a named structure
so a single field can hold one of several concrete types.

### Protocols a Wii U title uses

| ID | Hex | Protocol | Used for |
| --- | --- | --- | --- |
| 3 | `0x03` | NAT Traversal | Coordinating peer to peer probes |
| 10 | `0x0A` | Ticket Granting | Login and ticket requests on the auth server |
| 11 | `0x0B` | Secure Connection | Registering a client and its endpoints |
| 25 | `0x19` | Account Management | Account operations from inside a NEX session |
| 21 | `0x15` | Match Making | Gatherings, the older matchmaking interface |
| 50 | `0x32` | Match Making Ext | Participation management |
| 102 | `0x66` | Friends (Wii U) | Friend list, presence, requests, blocking |
| 109 | `0x6D` | Matchmake Extension | The matchmaking interface modern titles use |
| 112 | `0x70` | Ranking | Score tables, including event scoring |

## Matchmaking and peer to peer play

NEX matchmaking is a directory service, not a relay. The server tracks who is
looking for a game and hands out addresses; the match itself runs directly
between consoles.

A **gathering** is the generic container for a group of players. A **matchmake
session** is the gathering subtype used for a game lobby, carrying the game
mode, the stage, a player limit, an open or closed participation flag and a set
of application defined attributes that titles use to encode their own
matchmaking rules.

Two paths lead into a session. A client can create one and wait, or it can ask
the server to place it automatically, describing what it wants through search
criteria over those same attributes. The automatic path either joins a session
that matches or creates one when nothing fits.

```{mermaid}
sequenceDiagram
    autonumber
    participant A as Joining console
    participant S as Secure server
    participant H as Host console

    A->>S: Register, reporting its candidate endpoints
    A->>S: Auto matchmake with search criteria
    S-->>A: gathering ID and the host endpoints
    S-->>H: notification, a player joined
    A->>S: request NAT probe against the host
    S-->>H: probe request
    A-->>H: direct UDP probes in both directions
    A->>S: report the traversal result
    A-->>H: game traffic, no server involved
```

**Station URLs** are how endpoints travel. Each is a string naming a transport
and its parameters, an address and port, a connection ID, and NAT type
information. A console typically reports several: its local address, the
address a router mapped for it, and any relay candidate.

**NAT traversal** works by having the server ask both sides to send probes at
the same moment, punching a hole in each NAT. When both sides report success the
match proceeds directly. The server never carries game traffic, which is why the
addresses it hands out have to be reachable from the players' networks rather
than from the server's.

**Host migration.** Because one console hosts the session, its departure would
end the match. The protocol therefore allows ownership of the gathering and the
hosting role to move to another participant.

## Friends and presence

The friends service runs on its own NEX server, independent of any game, and
stays connected while the console is on. It holds the friend list, incoming and
outgoing friend requests, a block list, per user comments and preferences, and
the Mii shown next to each name.

Presence is the interesting part. A client publishes what it is doing, including
the title it is running and the gathering it sits in, and the server pushes
updates to everyone who has that user as a friend. That is what makes a friend's
game joinable straight from the friend list: the presence record carries enough
information to reach the session.

Notifications flow as RMC requests from server to client that expect no
response, covering a friend coming online, a request arriving, a request being
accepted, and presence changing.

## BOSS, the background download service

BOSS, branded SpotPass, delivers content to a console without the user asking.
It is plain HTTPS, split across three hosts.

```{admonition} Credit
:class: seealso

The BOSS internals, including the encryption and signing of payloads, are
documented thanks to the work of [Pretendo Network](https://pretendo.network/).
```

The console fetches a **task sheet** naming the files a task carries, each with a
size and a hash, then downloads the payloads themselves from the CDN host. A
separate policy list host tells the console how often to poll.

Payloads are not plain files. Each is encrypted with AES and authenticated with
an HMAC, both under keys held in the console's BOSS title, then wrapped in a
container carrying the parameters needed to decrypt it. A console will not
accept a payload whose hash or signature fails.

What sits inside the payload is title specific. Splatoon uses BOSS for the data
that changes without a game patch: the stage and mode rotation schedule, and
Splatfest announcements including the team names, the dialogue the announcers
read and the textures the plaza displays. Those arrive as **BYAML** documents,
Nintendo's binary form of YAML, and the textures as **BFRES** archives holding
GX2 format images ready for the console GPU.

Because the schedule arrives through a background download rather than a live
query, a change only reaches players on their next SpotPass fetch.
