#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OUT=$(mktemp -d /tmp/velaride-countdown-test.XXXXXX)
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -pthread -I"$HERE/../../app/velaride" \
  -I"$HERE/../ride_session/stubs" \
  "$HERE/test.c" "$HERE/../../app/velaride/velaride_countdown.c" -o "$OUT/test"
"$OUT/test"
