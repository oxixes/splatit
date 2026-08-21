# syntax=docker/dockerfile:1.7

# ---------------------------------------------------------------------------
# Build stage
#
# vcpkg builds gRPC, Protobuf, OpenSSL, Boost and the rest from source, so the
# first build takes a long time (roughly 45-90 minutes on a 4 core machine) and
# needs around 15 GB of free disk space. Later builds reuse the cache mounts.
#
# The submodules under lib/ must exist before building. Run
#   git submodule update --init --recursive
# in the checkout, otherwise the build fails early with a clear message.
# ---------------------------------------------------------------------------
FROM debian:trixie-slim AS build

ARG VCPKG_BASELINE=0cb95c860ea83aafc1b24350510b30dec535989a
ARG BUILD_TYPE=Release
ARG VCPKG_TRIPLET=x64-linux

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        zip \
        unzip \
        tar \
        pkg-config \
        ca-certificates \
        python3 \
        autoconf \
        automake \
        autoconf-archive \
        libtool \
        bison \
        flex \
        gettext \
        linux-libc-dev \
    && rm -rf /var/lib/apt/lists/*

# Check out the vcpkg commit that vcpkg.json pins as its builtin-baseline.
#
# This has to be a full clone. In manifest mode vcpkg resolves each port version
# by reading tree objects out of the repository history, so a shallow clone fails
# with "failed to unpack tree object" as soon as it tries. The clone only lives
# in this build stage, so its size never reaches the runtime image.
RUN git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout "$VCPKG_BASELINE" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src

# Resolve the manifest on its own first, so editing sources does not throw away
# the dependency build.
COPY vcpkg.json ./
RUN --mount=type=cache,target=/root/.cache/vcpkg,sharing=locked \
    "$VCPKG_ROOT/vcpkg" install --triplet "$VCPKG_TRIPLET" --x-install-root=/src/vcpkg_installed

COPY . .

RUN test -f lib/pugixml/CMakeLists.txt \
    && test -f lib/json-schema-validator/CMakeLists.txt \
    && test -f lib/stb/stb_image.h \
    || (echo "ERROR: git submodules are missing. Run 'git submodule update --init --recursive' before building." >&2 && exit 1)

RUN --mount=type=cache,target=/root/.cache/vcpkg,sharing=locked \
    cmake -B build -S . -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" \
        -DVCPKG_INSTALLED_DIR=/src/vcpkg_installed \
    && cmake --build build --parallel

# ---------------------------------------------------------------------------
# Runtime stage
# ---------------------------------------------------------------------------
FROM debian:trixie-slim AS runtime

# tzdata matters: on Linux the date library reads the operating system time zone
# database, and the account server refuses to start without it.
#
# curl is what the Compose health check calls. Removing it does not fail the
# build, it just leaves every container permanently unhealthy.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        tzdata \
        curl \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd --system --gid 10001 splatit \
    && useradd --system --uid 10001 --gid 10001 --home-dir /app --no-create-home splatit

WORKDIR /app

COPY --from=build /src/build/splatoon_server /usr/local/bin/splatoon_server

# The server resolves these by relative path, so they have to sit in the
# working directory next to the binary at run time.
COPY settings.schema.json boss.schema.json account_settings.html ./
COPY countries_languages.json regions.json timezones.json ./
COPY miiDefault.png miiDefault.tga ./
COPY PanelTexture.default.png BodyTeamA.default.png BodyTeamB.default.png ./

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh && chown -R splatit:splatit /app

VOLUME ["/app/data"]

# Account and BOSS over HTTPS
EXPOSE 443/tcp
# friends auth, friends secure, Splatoon auth, Splatoon secure (PRUDP over UDP)
EXPOSE 1201/udp 1202/udp 1203/udp 1204/udp
# gRPC control plane
EXPOSE 1999/tcp
# Management REST API
EXPOSE 3000/tcp

USER splatit

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["--data", "/app/data", "--log-level", "1"]
