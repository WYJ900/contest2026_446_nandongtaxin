#!/usr/bin/env bash
set -euo pipefail
export PATH=/opt/arm-gnu-toolchain/bin:/usr/local/bin:/usr/bin:/bin
WORK=/mnt/d/desktop/VelaRide_AI
STAGE=/home/wyj/velaride_product_features
SAVE=$WORK/backup/display-completion-20261104
OUT=$STAGE/cmake_out/lckfb_huangshan_pi
python3 "$WORK/my-contest-repo/tests/test_lcd_completion.py" > "$SAVE/lcd-tests.log" 2>&1
cd "$STAGE/nuttx"
cmake -B "$OUT" -S "$STAGE/nuttx" -GNinja \
  -DBOARD_CONFIG="$STAGE/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/configure.log" 2>&1
cmake --build "$OUT" -- -j8 > "$SAVE/build.log" 2>&1 || { tail -50 "$SAVE/build.log"; exit 1; }
cmp "$WORK/my-contest-repo/artifacts/install-stage-20261104/config" "$OUT/.config"
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/firmware.bin"
cp "$OUT/.config" "$SAVE/config"
tar -czf "$SAVE/app-source.tar.gz" -C "$STAGE/apps/examples" velaride
tail -8 "$SAVE/build.log"
cat "$SAVE/lcd-tests.log"
sha256sum "$SAVE/firmware.bin"
