#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$(mktemp /tmp/velaride-memo-test.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
cc -D_GNU_SOURCE -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -pthread -I"$HERE/../ride_session/stubs" "$HERE/test.c" -o "$BIN"
"$BIN"
