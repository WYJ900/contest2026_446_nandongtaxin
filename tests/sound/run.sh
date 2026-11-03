#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
APP=$HERE/../../app/velaride
OUT=$(mktemp -d /tmp/velaride-sound-test.XXXXXX)
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -pthread -I"$HERE/stubs" -I"$HERE/../ride_session/stubs" \
  "$HERE/test.c" "$APP/velaride_sound_render.c" "$APP/velaride_sound_assets.c" \
  -o "$OUT/test"
"$OUT/test"
cc -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
  -fsanitize=address,undefined -g -I"$HERE/board_stubs" -I"$HERE/stubs" \
  -I"$HERE/../ride_session/stubs" "$HERE/board_test.c" -o "$OUT/board_test"
"$OUT/board_test"
