#!/bin/sh
set -e

DATA_DIR="/app/data"

# The server reads the data directory from --data, but it only creates the top
# level directory itself. The certificate manager and the Mii store both expect
# their parent directories to exist already, so create them up front.
for i in $(seq 1 "$#"); do
    eval "arg=\${$i}"
    case "$arg" in
        --data=*) DATA_DIR="${arg#--data=}" ;;
        -d|--data)
            next=$((i + 1))
            eval "DATA_DIR=\${$next}"
            ;;
    esac
done

mkdir -p "$DATA_DIR/certs" "$DATA_DIR/miis" "$DATA_DIR/boss"

exec /usr/local/bin/splatoon_server "$@"
