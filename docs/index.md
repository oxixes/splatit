# SplatIt Server

SplatIt is a from-scratch reimplementation of the online services that Splatoon
for Wii U talked to. It speaks the Nintendo Network account protocol, the BOSS
content delivery protocol and the NEX game protocols, so an original console or
an emulator can sign in, add friends, matchmake and play again after the
official servers went dark.

The whole thing is a single C++20 executable. One process can host every
subsystem on a laptop, or a handful of processes can split the work across
machines and share their live state through Redis. A React admin panel sits on
top of a REST management API for day to day operation.

```{admonition} Scope and legality
:class: warning

This project reimplements protocols. It ships no Nintendo code and no game
assets. Running it still requires a legitimate copy of the game, and on the
emulator path it requires files dumped from a console you own. Check what your
local law allows before deploying anything.
```

## Where to start

::::{grid} 1 1 2 2
:gutter: 3

:::{grid-item-card} Build it
:link: building
:link-type: doc

Toolchain, vcpkg dependencies, the manual time zone download that Windows
needs, and how to compile the admin panel.
:::

:::{grid-item-card} Understand it
:link: architecture
:link-type: doc

How Nintendo's services worked: the login chain, PRUDP, the Kerberos ticket
scheme and NEX matchmaking.
:::

:::{grid-item-card} Deploy it
:link: deployment
:link-type: doc

Docker images, single node and distributed Compose stacks, DNS, certificates
and TLS between nodes.
:::

:::{grid-item-card} Run it
:link: management
:link-type: doc

The admin panel, the management REST API, accounts, devices, Splatfests and
map rotation.
:::

::::

## Contents

```{toctree}
:maxdepth: 2
:caption: Getting started

overview
building
configuration
```

```{toctree}
:maxdepth: 2
:caption: How it works

architecture
servers
```

```{toctree}
:maxdepth: 2
:caption: Running a server

deployment
connecting
management
```

```{toctree}
:maxdepth: 2
:caption: Project

roadmap
```
