#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OUT=$(mktemp -d /tmp/velaride-night-test.XXXXXX)
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -pthread -I"$HERE/../ride_session/stubs" \
  "$HERE/test.c" -o "$OUT/test"
"$OUT/test"
