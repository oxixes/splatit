#!/usr/bin/env bash
#
# Assemble a release package for one platform.
#
#   packaging/package.sh <platform> <build-dir> <exe-name>
#
# Example:
#   packaging/package.sh linux-x64   build         splatoon_server
#   packaging/package.sh windows-x64 build/Release splatoon_server.exe
#
# Produces dist/splatit-server-<platform>/ and an archive beside it. Runs from
# the repository root and works the same on the three CI runners, so the layout
# a release ships is the layout this script was tested with.

set -euo pipefail

PLATFORM="${1:?usage: package.sh <platform> <build-dir> <exe-name>}"
BUILD_DIR="${2:?usage: package.sh <platform> <build-dir> <exe-name>}"
EXE="${3:?usage: package.sh <platform> <build-dir> <exe-name>}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

NAME="splatit-server-${PLATFORM}"
OUT="dist/${NAME}"

rm -rf "$OUT"
mkdir -p "$OUT"

# The binary.
if [ ! -f "${BUILD_DIR}/${EXE}" ]; then
    echo "error: ${BUILD_DIR}/${EXE} not found, did the build run?" >&2
    exit 1
fi
cp "${BUILD_DIR}/${EXE}" "$OUT/"

# On Windows the vcpkg triplet is dynamic, so the DLLs CMake copied next to the
# executable have to travel with it.
case "$PLATFORM" in
    windows-*)
        shopt -s nullglob
        dlls=("${BUILD_DIR}"/*.dll)
        if [ ${#dlls[@]} -eq 0 ]; then
            echo "error: no DLLs beside the executable, the app-local deploy step did not run" >&2
            exit 1
        fi
        cp "${dlls[@]}" "$OUT/"
        echo "bundled ${#dlls[@]} DLLs"
        cp packaging/fetch-tzdata.ps1 "$OUT/"
        ;;
esac

# The static files the server opens by relative path. A missing one only shows
# up when a request happens to need it, so fail the build instead.
missing=0
count=0
while IFS= read -r line; do
    line="${line%%#*}"
    line="$(echo "$line" | tr -d '[:space:]')"
    [ -z "$line" ] && continue
    if [ ! -f "$line" ]; then
        echo "error: runtime file '$line' is missing from the repository" >&2
        missing=$((missing + 1))
        continue
    fi
    cp "$line" "$OUT/"
    count=$((count + 1))
done < packaging/runtime-files.txt

if [ "$missing" -ne 0 ]; then
    echo "error: $missing runtime file(s) missing" >&2
    exit 1
fi
echo "bundled $count runtime files"

# An empty data directory, so the first run has somewhere to put the settings
# file and the generated certificates.
mkdir -p "$OUT/data/certs" "$OUT/data/miis"

cp packaging/artifact-README.md "$OUT/README.md"

# Archive. tar.gz on the Unix platforms because it keeps the executable bit,
# zip on Windows because that is what people expect there. cmake -E tar ships
# with CMake on every runner, so one tool covers both.
mkdir -p dist
case "$PLATFORM" in
    windows-*)
        (cd dist && cmake -E tar cf "${NAME}.zip" --format=zip "$NAME")
        echo "archive=dist/${NAME}.zip"
        ;;
    *)
        (cd dist && cmake -E tar czf "${NAME}.tar.gz" "$NAME")
        echo "archive=dist/${NAME}.tar.gz"
        ;;
esac

echo "packaged $OUT"
ls -la "$OUT"
