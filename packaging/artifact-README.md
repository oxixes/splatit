# SplatIt Server

A reimplementation of the online services behind Splatoon for Wii U.

Full documentation: <https://github.com/oxixes/splatit>

## What is in this archive

```text
splatoon_server[.exe]        the server
*.dll                        bundled dependencies, Windows only; the Microsoft
                             C and C++ runtime is not among them, see below
settings.schema.json         validates data/settings.json
boss.schema.json             validates the BOSS manifest
account_settings.html        the in-game account settings web view
countries_languages.json     country and language tables
regions.json                 console region tables
timezones.json               time zone tables
miiDefault.png / .tga        fallback Mii images
PanelTexture.default.png     default Splatfest panel texture
BodyTeamA.default.png        default team A texture
BodyTeamB.default.png        default team B texture
data/                        empty data directory, ready for the first run
fetch-tzdata.ps1             time zone database installer (Windows only)
```

The server opens every file in that list by relative path, so run it from this
directory. Moving the binary somewhere else on its own will not work.

## Windows: install the Visual C++ Redistributable

The `.dll` files beside the executable cover the libraries this server was built
against, but not the Microsoft C and C++ runtime itself. Without it Windows
refuses to start the process, reporting `VCRUNTIME140.dll` or `MSVCP140.dll` as
missing.

Install the x64 **Visual C++ Redistributable for Visual Studio 2015-2022** from
Microsoft, or through a package manager:

```text
winget install Microsoft.VCRedist.2015+.x64
```

Machines with Visual Studio, or with almost any other native application
installed, usually have it already.

Linux and macOS need no equivalent. Every dependency is linked statically into
the binary there, so it relies only on the system C and C++ runtime.

## Windows: install the time zone database first

The server needs the IANA time zone database, and Windows does not provide one
in a form it can read. Nothing fails at startup; the account server throws on the
first request that touches a user's local time.

```powershell
powershell -ExecutionPolicy Bypass -File fetch-tzdata.ps1
```

Linux and macOS need nothing here, since the server reads the system database.
On a minimal Linux install, make sure the `tzdata` package is present.

## Running it

```text
splatoon_server --data ./data --log-level 1
```

The first start writes `data/settings.json` with freshly generated keys, then
stops being useful until that file is edited. At minimum, set the addresses the
console reaches. The configuration reference in the documentation covers every
key.

Options:

```text
-d, --data       Data directory path (default: ./data)
-l, --log-level  Minimum log level, 0 debug to 3 failure (default: 1)
-h, --help       Show this message
```

## The admin panel

The web interface ships as a separate archive, `splatit-web`. It is a static
single page application, so any web server can host it. Point the server at the
extracted directory and make sure unmatched paths fall back to `index.html`,
since the panel routes on the client.

Edit `config.json` in that directory so `apiUrl` points at the management API
before serving it.

## Default administrator

The first run creates an account named `admin` with the password `splatit`.
Change it through the admin panel before the server reaches an untrusted
network.
