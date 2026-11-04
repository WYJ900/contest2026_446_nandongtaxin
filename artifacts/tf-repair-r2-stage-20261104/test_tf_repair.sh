#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
STAGE=/home/wyj/velaride_product_features
SAVE=$WORK/backup/tf-repair-r2-20261104
FIXTURE=/tmp/velaride-tf-ui-fixture
OUT=/tmp/velaride-tf-ui-test
mkdir -p "$FIXTURE/app/velaride" "$SAVE/screens"
rsync -a "$STAGE/apps/examples/velaride/" "$FIXTURE/app/velaride/"
python3 - "$WORK" "$FIXTURE" <<'PY'
import sys,zipfile
from pathlib import Path
work,fixture=map(Path,sys.argv[1:])
with zipfile.ZipFile(work/'my-contest-repo/artifacts/install-stage-20261104/tests-source.zip') as z:
    z.extractall(fixture)
PY
cmake -S "$FIXTURE/tests/sport_ui" -B "$OUT" -GNinja \
  -DLVGL_SOURCE="$STAGE/apps/graphics/lvgl/lvgl" \
  -DVELARIDE_COUNTDOWN=ON -DVELARIDE_ALARM=ON -DVELARIDE_SOUND=ON -DVELARIDE_NIGHT=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > "$SAVE/ui-configure.log" 2>&1
cmake --build "$OUT" -- -j8 > "$SAVE/ui-build.log" 2>&1
"$OUT/sport_ui_test" "$SAVE/screens" > "$SAVE/ui-sport.log" 2>&1
"$OUT/shell_ui_test" "$SAVE/screens" > "$SAVE/ui-shell.log" 2>&1
bash "$FIXTURE/tests/ride_session/run.sh" > "$SAVE/ride-tests.log" 2>&1
python3 "$WORK/my-contest-repo/tests/test_tf_spi_recovery.py" > "$SAVE/driver-tests.log" 2>&1
tail -1 "$SAVE/ui-sport.log"
tail -1 "$SAVE/ui-shell.log"
tail -1 "$SAVE/ride-tests.log"
cat "$SAVE/driver-tests.log"
