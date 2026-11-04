#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$(mktemp /tmp/velaride-phone-test.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
cc -D_GNU_SOURCE -std=c11 -O1 -g -Wall -Wextra -Werror -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$HERE/../ride_session/stubs" "$HERE/test.c" -o "$BIN"
"$BIN"
