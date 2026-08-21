# Building from source

## Prebuilt downloads

Every release carries archives built by CI for Linux, Windows and macOS, plus one
for the admin panel. Each server archive holds the binary together with the JSON
schemas, the account server content files and the default textures the server
opens by relative path, so it runs from the directory it extracts into.

On Windows the archive also carries `fetch-tzdata.ps1`, which installs the time
zone database described below. Run it once before starting the server.

Building from source is only necessary to change the code, to target a platform
CI does not cover, or to build the container image.

## What you need

| Requirement | Minimum | Notes |
| --- | --- | --- |
| C++ compiler | MSVC 19.34, GCC 13, Clang 17 | C++20 coroutines, plus `std::format`, which GCC only shipped in 13 and libc++ in 17 |
| CMake | 3.22 | Ninja works well and speeds up the build |
| vcpkg | Any recent checkout | The manifest pins its own baseline commit |
| Git | Any | Three dependencies come in as submodules |
| Disk space | About 15 GB | vcpkg builds gRPC, Boost and OpenSSL from source |
| Node.js | 20 | Only for the admin panel |

The first vcpkg run takes a long time, usually between 45 and 90 minutes,
because gRPC and Boost compile from source. Later builds reuse the vcpkg binary
cache and finish in seconds.

## Getting the sources

```bash
git clone https://github.com/oxixes/splatit.git
cd splatit
git submodule update --init --recursive
```

The submodules under `lib/` are pugixml, json-schema-validator and stb. CMake
fails immediately without them.

## Windows

### Install the toolchain

1. Install Visual Studio 2022 with the *Desktop development with C++* workload,
   which brings MSVC v143, the Windows SDK, CMake and Ninja.
2. Install vcpkg somewhere short, for example `C:\vcpkg`:

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```

   CLion ships its own vcpkg under `%USERPROFILE%\.vcpkg-clion\vcpkg`, and that
   works just as well.

### Download the time zone database

The project uses Howard Hinnant's `date` library for time zone conversions. On
Windows, vcpkg builds that library with `USE_OS_TZDB=0` and `HAS_REMOTE_API=0`,
so the library neither reads the Windows registry zones nor downloads anything
on its own. Skipping this step makes the account server throw as soon as it
converts a user's local time, which happens on the very first profile request.

```{admonition} This step is mandatory on Windows
:class: warning

Nothing in the build fails without the time zone database. The failure shows up
at run time, and it looks like an unrelated account server error.
```

Do this once:

1. Download the current IANA time zone database from
   <https://www.iana.org/time-zones>. Take the `tzdataXXXXx.tar.gz` file, for
   example `tzdata2025b.tar.gz`.
2. Extract it into `%USERPROFILE%\Downloads\tzdata`. The `date` library looks in
   the Downloads known folder by default, so that exact path matters. If the
   Downloads folder is redirected, follow the redirection.
3. Download `windowsZones.xml` from the Unicode CLDR project and put it in the
   same folder:

   ```powershell
   Invoke-WebRequest -Uri "https://raw.githubusercontent.com/unicode-org/cldr/main/common/supplemental/windowsZones.xml" -OutFile "$env:USERPROFILE\Downloads\tzdata\windowsZones.xml"
   ```

   Windows reports time zones under names like *Romance Standard Time*, and this
   file maps those onto IANA names like *Europe/Madrid*. The library refuses to
   resolve any zone without it.

The folder should end up looking like this:

```text
%USERPROFILE%\Downloads\tzdata\
    africa
    antarctica
    asia
    australasia
    backward
    etcetera
    europe
    leapseconds
    northamerica
    southamerica
    version
    windowsZones.xml
    zone.tab
    ...
```

To put the database somewhere else, call `date::set_install()` before the first
time zone lookup, or rebuild the `date` port with a different `INSTALL`
definition.

### Configure and build

From a *Developer PowerShell for VS 2022*:

```powershell
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

```powershell
cmake --build build --parallel
```

vcpkg reads `vcpkg.json`, installs everything into `build/vcpkg_installed` and
copies the runtime DLLs next to `build/splatoon_server.exe`.

### CLion

CLion handles all of this through *Settings, Build, Execution, Deployment,
CMake*: add a Release profile, set the generator to Ninja and pick the bundled
vcpkg toolchain. CLion detects `vcpkg.json` and offers to install the manifest
dependencies on its own.

Set the working directory of the run configuration to the repository root.
The server resolves `settings.schema.json`, `timezones.json` and the default
images by relative path, so it only starts from there.

## Linux

### Install the toolchain

The distribution has to be new enough to carry GCC 13. Debian 13 and Ubuntu
24.04 both do; Debian 12 ships GCC 12, which fails on `std::format` partway
through the build.

On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf automake autoconf-archive libtool bison flex gettext python3 tzdata
```

On Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build git curl zip unzip tar pkgconf-pkg-config autoconf automake libtool bison flex gettext python3 tzdata
```

The `tzdata` package matters. On Linux, vcpkg builds the `date` library against
the operating system time zone database, so it reads `/usr/share/zoneinfo`
rather than a downloaded copy. That is the whole reason the manual download only
applies to Windows.

### Install vcpkg

```bash
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg
```

### Configure and build

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

```bash
cmake --build build --parallel
```

The binary lands at `build/splatoon_server`.

## Building with Docker

The repository ships a multi-stage `Dockerfile` that performs the whole vcpkg
build in a container, so the host needs nothing beyond Docker itself:

```bash
git submodule update --init --recursive && docker build -t splatit/server:latest .
```

[Deployment](deployment.md) covers the image and the Compose stacks in detail.

## Dependencies

vcpkg resolves these from `vcpkg.json`:

| Package | Used for |
| --- | --- |
| `openssl` | TLS, certificate generation, RC4, AES, HMAC, JWT signing |
| `grpc` and `protobuf` | The control plane between subsystems |
| `sqlite3` | The SQLite database backend |
| `libpq` | The PostgreSQL database backend |
| `hiredis` with `ssl` | Redis shared state |
| `nlohmann-json` | Settings, manifests and the management API |
| `pugixml` | XML for the account server and `network_services.xml` |
| `date` | Time zone conversion |
| `mailio` | Sending verification and password reset email |
| `boost-uuid` | Identifier generation |

Three more come in as git submodules under `lib/`:

| Submodule | Used for |
| --- | --- |
| `pboettch/json-schema-validator` | Validating `settings.json` and the BOSS manifest |
| `zeux/pugixml` | XML parsing |
| `nothings/stb` | Decoding PNG images when building BFRES textures |

## Runtime files

The server reads several files by relative path, so they must sit in the working
directory when it starts:

```text
settings.schema.json        validates data/settings.json
boss.schema.json            validates the BOSS manifest
account_settings.html       the in-game account settings web view
countries_languages.json    country and language tables
regions.json                console region tables
timezones.json              time zone tables offered to the account settings UI
miiDefault.png              fallback Mii image
miiDefault.tga              fallback Mii image, TGA variant
PanelTexture.default.png    default Splatfest panel texture
BodyTeamA.default.png       default team A body texture
BodyTeamB.default.png       default team B body texture
```

Everything else goes in the data directory, which defaults to `./data` and moves
with `--data`.

## Building the admin panel

```bash
cd web && npm install && npm run build
```

The panel builds as a single page application, so `build/client` is a static
site that any web server can host. During development, `npm run dev` starts
Vite with hot reloading on port 5173 instead.

The panel reads `/config.json` at run time rather than at build time, so
pointing a built bundle at a different management API only takes editing
`web/public/config.json`, or mounting a replacement over
`/usr/share/nginx/html/config.json` in the container:

```json
{
  "apiUrl": "http://localhost:3000",
  "compatibleVersions": [1]
}
```

`compatibleVersions` lists the management API versions this build understands.
The panel compares it against the `version` field that `GET /api/v1/status`
returns and warns when they disagree.

## Building the documentation

The pages you are reading build with Sphinx:

```bash
pip install -r docs/requirements.txt
```

```bash
python -m sphinx -b html docs docs/_build/html
```

Serve the result to read it with working navigation and search:

```bash
python -m http.server 8123 --directory docs/_build/html
```

Read the Docs runs the same build through `.readthedocs.yaml`. Missing
screenshots produce warnings rather than errors, so the build succeeds with the
alt text in their place until the images land in `docs/_static/img`.

## Command line options

```text
splatoon_server [options]
  -d, --data       Data directory path (default: ./data)
  -l, --log-level  Minimum log level, 0 to 3 (default: 1)
  -h, --help       Show this message
```

Log levels run 0 for debug, 1 for info, 2 for warnings and 3 for failures.
