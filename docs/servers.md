# The services in detail

A closer look at each service a Wii U title depends on. As in the previous page,
this describes Nintendo's systems rather than any particular reimplementation of
them, and it follows the naming used by
[kinnay's NintendoClients wiki](https://github.com/kinnay/NintendoClients/wiki).

## The account server

`account.nintendo.net` owns identity. It issues every credential the other
services check, so a console that cannot reach it cannot play online at all.

### What it holds

An NNID record carries the username, a password hash, birth date, country,
region, time zone, gender, an email address and its verification state, plus
marketing and parental control flags. The **principal ID**, a 32 bit number, is
the identity every other service uses; the username exists for humans.

Each account links to one or more **devices**. A device record carries the
device ID, serial number, region and the certificate the console presents.
Linking is what lets an account sign in from a particular console, and the
server tracks per device attributes alongside the link.

Each account also has one or more **Miis**, one marked primary. The Mii travels
as a binary blob, and a companion host serves pre-rendered images of it so other
consoles can display a face without parsing the format.

### The request shape

Requests are HTTPS with XML bodies. Every one carries the console's client
certificate plus headers describing the device, and authenticated calls add a
bearer token. Errors come back as XML documents with a numeric code, which is
what the console surfaces as those opaque error numbers.

The endpoints group into a few families:

| Family | Purpose |
| --- | --- |
| `oauth20` | Exchange credentials for an access token, and refresh it |
| `provider` | Obtain a NEX token for a game server, or a service token |
| `people` | Read and update the profile, email, Miis, devices and agreements |
| `devices` | Report and change the state of the console itself |
| `admin` | Server time, and translation between PIDs and usernames |
| `support` | Email validation and confirmation resends |

### Agreements

The server stores the current EULA and privacy policy per country and language,
each with a version. A console checks the version it has accepted against the
current one and blocks the user until they accept, which is how a policy change
reaches every player without a title update.

## The BOSS servers

Three hosts make up BOSS, and a console visits them in order:

| Host | Serves |
| --- | --- |
| `nppl.app.nintendo.net` | The policy list, telling the console how often to poll |
| `npts.app.nintendo.net` | Task sheets, listing the files a task carries |
| `npdi.cdn.nintendo.net` | The file payloads themselves |

A task belongs to an **application ID**, an opaque sixteen character string
identifying the title and region, which is why the same game carries different
BOSS identifiers across territories. The task sheet names each file with its
size and an MD5 hash, so the console can skip anything it already holds and
reject anything that arrives corrupted.

The encryption and signing of the payloads, and the container format around
them, are documented thanks to [Pretendo Network](https://pretendo.network/).

## NEX authentication servers

Every title with online play has a matching pair of NEX servers. The
authentication server is small and does one job: turn a NEX token into a ticket
for the secure server.

It speaks the **Ticket Granting** protocol, ID 10:

| Method | Name | Purpose |
| --- | --- | --- |
| 1 | `Login` | Sign in by name, used by the friends service |
| 2 | `LoginEx` | Sign in with a signed token, used by game titles |
| 3 | `RequestTicket` | Obtain a ticket for a named target server |

`Login` predates the token scheme and identifies the caller by name.
`LoginEx` carries the encrypted blob the account server issued, which the auth
server validates before it will produce anything. Either way the response
carries the Kerberos ticket described in the previous page, along with a station
URL pointing at the secure server the client should connect to next.

Auth servers hold no game state. Their databases exist only to record which
credentials they have issued.

## The friends server

The friends service is a NEX server like any other, running under server ID
`00003200`, but it belongs to the system rather than to a game. Its secure
server speaks the **Friends (Wii U)** protocol, ID 102, whose roughly twenty
methods cover four areas:

**The friend list.** Adding a friend by principal ID or by name, removing one,
and fetching the full list with each entry's status.

**Friend requests.** Sending, cancelling, accepting, denying and deleting a
request, plus marking requests as seen. Requests carry a short message.

**Blocking.** Adding and removing entries on a block list, which suppresses both
requests and visibility.

**Profile and presence.** Publishing a comment, a Mii, presence and preferences,
and reading back the basic information of other users.

Two more protocols appear on the same server: **Secure Connection**, ID 11, for
registering the client and its endpoints after connecting, and **Account
Management**, ID 25, which carries account operations that have to happen from
inside a NEX session rather than over HTTPS.

## Game secure servers

The secure server for a title is where the game itself lives. For Splatoon that
is server ID `10162B00`, and it combines several protocols.

| Protocol | ID | What the game uses it for |
| --- | --- | --- |
| Secure Connection | 11 | Registering after connect, reporting endpoints, replacing a station URL as the network changes |
| NAT Traversal | 3 | Requesting probes between peers and reporting the outcome |
| Match Making | 21 | Looking a gathering up, reading its endpoints, changing its host or owner, unregistering it |
| Match Making Ext | 50 | Ending participation in a gathering |
| Matchmake Extension | 109 | Creating, joining and automatically matching sessions, opening and closing participation, updating attributes and progress |
| Ranking | 112 | Reading and uploading scores, which titles reuse for event scoring |

The division between protocols 21 and 109 is historical. The older interface
handles the gathering as an object, while the newer one carries the parameter
structures that titles actually populate, so a game generally creates and joins
through 109 and falls back to 21 for lookups and ownership changes.

**Session attributes** are the mechanism that makes one generic matchmaking
service work for every title. The server treats them as opaque numbers and
matches on equality or range, while the game decides that a particular slot
means a game mode, a stage, a rank band or a language. A server operator
changing matchmaking behaviour is really changing how those numbers are
interpreted.

**Event scoring** works through the ranking protocol. Players upload a score
after each match, tagged with the team or category they belong to, and the
server aggregates per category. Splatfest results come from exactly this.

## What the console needs to reach

Pulling the addressing together, an online session touches these endpoints:

| Endpoint | Transport | Role |
| --- | --- | --- |
| `account.nintendo.net` | HTTPS | Identity and tokens |
| `mii-secure.account.nintendo.net` | HTTPS | Rendered Mii images |
| `nppl.app.nintendo.net` | HTTPS | BOSS policy list |
| `npts.app.nintendo.net` | HTTPS | BOSS task sheets |
| `npdi.cdn.nintendo.net` | HTTPS | BOSS payloads |
| Friends auth and secure | UDP | Friend list and presence |
| Game auth and secure | UDP | Matchmaking and scoring |
| Other consoles | UDP | The match itself |

The last row is the one people forget. Once matchmaking finishes, the servers
step aside and the consoles talk to each other directly.
