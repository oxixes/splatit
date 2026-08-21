# Overview

## What the server replaces

Splatoon for Wii U depended on several unrelated Nintendo services. Losing any
one of them breaks a different part of the game, so SplatIt reimplements all of
them:

| Original service | What the game uses it for | SplatIt subsystem |
| --- | --- | --- |
| Nintendo Network account server (`account.nintendo.net`) | Signing in, creating NNIDs, Mii data, service tokens, NEX tokens | Account server |
| BOSS / SpotPass (`npts.app`, `npdi.cdn`, `nppl.app`) | Downloading stage rotation, Splatfest data and panel textures | BOSS server |
| NEX friends authentication and secure servers | Friend lists, presence, friend requests, blocking | Friends auth and friends secure |
| NEX game authentication and secure servers | Lobbies, matchmaking, NAT traversal, Splatfest scores | Splatoon auth and Splatoon secure |

Two more pieces exist only in SplatIt and have no Nintendo counterpart:

- A **gRPC control plane** that lets the subsystems call each other, whether
  they run in the same process or on different machines.
- A **management API** and **admin panel** for creating accounts, minting
  emulator files, editing Splatfests and watching live lobbies.

## One binary, many roles

Every subsystem lives in the same executable and switches on through the
settings file. Setting `"enabled": false` on a section skips its database, its
sockets and its gRPC services entirely.

```{mermaid}
flowchart LR
    subgraph proc["splatoon_server process"]
        direction TB
        A["Account<br/>HTTPS"]
        B["BOSS<br/>HTTPS"]
        FA["Friends auth<br/>UDP"]
        FS["Friends secure<br/>UDP"]
        SA["Splatoon auth<br/>UDP"]
        SS["Splatoon secure<br/>UDP"]
        M["Management<br/>HTTP"]
        G["gRPC control plane"]
    end
    cfg["settings.json"] -->|enables or disables| proc
```

That design gives three deployment shapes, all documented in
[Deployment](deployment.md):

1. **Everything in one process.** SQLite files, in-process shared state, no
   network hops. Good for a home LAN or a development box.
2. **A gateway plus game nodes.** The account, BOSS and management subsystems
   run on one node; auth and secure server pairs run on others. PostgreSQL
   stores the data and Redis carries the live session state.
3. **Anything in between.** Nothing forces a particular split, because the
   subsystems only ever find each other through addresses in the settings file.

## Technology

- **C++20** with coroutines for every request handler. Handlers suspend on
  database queries and gRPC calls instead of blocking a thread.
- **Hand-written socket and protocol layers.** The HTTP parser, the PRUDP
  transport and the RMC dispatcher are all part of the repository, which keeps
  the wire behaviour close to what the game expects.
- **vcpkg** in manifest mode for dependencies, and CMake for the build.
- **SQLite or PostgreSQL** behind one database interface, chosen per subsystem.
- **Redis** for shared session state when more than one node runs.
- **React Router 7, Tailwind and shadcn/ui** for the admin panel.
