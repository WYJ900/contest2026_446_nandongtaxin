#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
NAME=${1:?stage name required}
STAMP=${2:-20261103}
SAVE=$WORK/backup/$NAME-$STAMP
STAGE=/home/wyj/velaride_product_features
FIXTURE=/tmp/velaride-features-ui-fixture
OUT=/tmp/velaride-features-ui-test
mkdir -p "$FIXTURE/app/velaride" "$FIXTURE/tests" "$SAVE/screens"
rsync -a "$STAGE/apps/examples/velaride/" "$FIXTURE/app/velaride/"
rsync -a "$WORK/my-contest-repo/tests/sport_ui" "$WORK/my-contest-repo/tests/ride_session" "$FIXTURE/tests/"
cmake -S "$FIXTURE/tests/sport_ui" -B "$OUT" -GNinja \
  -DLVGL_SOURCE="$STAGE/apps/graphics/lvgl/lvgl" \
  -DVELARIDE_COUNTDOWN=ON -DVELARIDE_ALARM=ON -DVELARIDE_SOUND=ON -DVELARIDE_NIGHT=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > "$SAVE/ui-configure.log" 2>&1
cmake --build "$OUT" -- -j8 > "$SAVE/ui-build.log" 2>&1 || { tail -40 "$SAVE/ui-build.log"; exit 1; }
"$OUT/sport_ui_test" "$SAVE/screens" > "$SAVE/ui-sport.log" 2>&1 || { tail -40 "$SAVE/ui-sport.log"; exit 1; }
"$OUT/shell_ui_test" "$SAVE/screens" > "$SAVE/ui-shell.log" 2>&1 || { tail -40 "$SAVE/ui-shell.log"; exit 1; }
bash "$WORK/my-contest-repo/tests/ride_session/run.sh" > "$SAVE/ride-tests.log" 2>&1
python3 "$WORK/my-contest-repo/tests/test_ble_safety_payload.py" > "$SAVE/ble-tests.log" 2>&1
if [ -f "$WORK/my-contest-repo/tests/memo/run.sh" ]; then
  bash "$WORK/my-contest-repo/tests/memo/run.sh" > "$SAVE/memo-tests.log" 2>&1 || { tail -40 "$SAVE/memo-tests.log"; exit 1; }
fi
tail -1 "$SAVE/ui-sport.log"
tail -1 "$SAVE/ui-shell.log"
tail -1 "$SAVE/ride-tests.log"
cat "$SAVE/ble-tests.log"
