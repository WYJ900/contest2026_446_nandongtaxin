#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
SAVE=$WORK/backup/safety-stage-20261103/ui
STAGE=/home/wyj/velaride_safety_stage
FIXTURE=/tmp/velaride-safety-ui-fixture
OUT=/tmp/velaride-safety-ui-test
mkdir -p "$FIXTURE/app/velaride" "$FIXTURE/tests" "$SAVE/screens"
rsync -a "$STAGE/apps/examples/velaride/" "$FIXTURE/app/velaride/"
rsync -a "$WORK/my-contest-repo/tests/sport_ui" "$WORK/my-contest-repo/tests/ride_session" "$FIXTURE/tests/"
cmake -S "$FIXTURE/tests/sport_ui" -B "$OUT" -GNinja \
  -DLVGL_SOURCE="$STAGE/apps/graphics/lvgl/lvgl" \
  -DVELARIDE_COUNTDOWN=ON -DVELARIDE_ALARM=ON -DVELARIDE_SOUND=ON -DVELARIDE_NIGHT=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > "$SAVE/01-configure.log" 2>&1
cmake --build "$OUT" -- -j8 > "$SAVE/02-build.log" 2>&1 ||
  { tail -40 "$SAVE/02-build.log"; exit 1; }
"$OUT/sport_ui_test" "$SAVE/screens" > "$SAVE/03-sport.log" 2>&1 ||
  { tail -45 "$SAVE/03-sport.log"; exit 1; }
"$OUT/shell_ui_test" "$SAVE/screens" > "$SAVE/04-shell.log" 2>&1 ||
  { tail -45 "$SAVE/04-shell.log"; exit 1; }
tail -1 "$SAVE/03-sport.log"
tail -1 "$SAVE/04-shell.log"
