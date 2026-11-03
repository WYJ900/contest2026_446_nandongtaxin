#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
SAVE=$WORK/backup/ride-gps-20261101/ui
STAGE=/home/wyj/velaride_ride_gps_stage
FIXTURE=/tmp/velaride-ride-gps-ui-fixture
OUT=/tmp/velaride-ride-gps-ui-test
mkdir -p "$FIXTURE/app/velaride" "$FIXTURE/tests" "$SAVE/screens"
# Test precisely the source tree used to build the installed candidate, not
# the canonical tree which may contain additional, unported UI changes.
rsync -a "$STAGE/apps/examples/velaride/" "$FIXTURE/app/velaride/"
rsync -a "$WORK/my-contest-repo/tests/sport_ui" "$WORK/my-contest-repo/tests/ride_session" "$FIXTURE/tests/"
cmake -S "$FIXTURE/tests/sport_ui" -B "$OUT" -GNinja \
  -DLVGL_SOURCE="$STAGE/apps/graphics/lvgl/lvgl" \
  -DVELARIDE_COUNTDOWN=ON -DVELARIDE_ALARM=ON -DVELARIDE_SOUND=ON -DVELARIDE_NIGHT=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > "$SAVE/05-ui-configure.log" 2>&1
cmake --build "$OUT" -- -j8 > "$SAVE/06-ui-build.log" 2>&1 ||
  { tail -50 "$SAVE/06-ui-build.log"; exit 1; }
"$OUT/sport_ui_test" "$SAVE/screens" > "$SAVE/07-sport-ui.log" 2>&1 ||
  { tail -40 "$SAVE/07-sport-ui.log"; exit 1; }
"$OUT/shell_ui_test" "$SAVE/screens" > "$SAVE/08-shell-ui.log" 2>&1 ||
  { tail -40 "$SAVE/08-shell-ui.log"; exit 1; }
tail -2 "$SAVE/07-sport-ui.log"
tail -2 "$SAVE/08-shell-ui.log"
