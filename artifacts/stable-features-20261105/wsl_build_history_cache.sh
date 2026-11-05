#!/bin/bash
# Clean tree (safety stage, original LCD driver) + activity-stage app + RAM history cache.
set -euo pipefail
export PATH=/opt/arm-gnu-toolchain/bin:/home/wyj/.local/bin:/usr/local/bin:/usr/bin:/bin
WORK=/mnt/d/desktop/VelaRide_AI
BASE=/home/wyj/velaride_safety_stage
DST=/home/wyj/velaride_history_cache
SAVE=$WORK/backup/sensor-lock-20261105
APP=$DST/apps/examples/velaride
OUT=$DST/cmake_out/lckfb_huangshan_pi
mkdir -p "$SAVE"
if [ ! -e "$DST/.copied" ]; then
  rm -rf "$DST"; mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.copied"
fi
rm -rf "$APP"; mkdir -p "$DST/apps/examples"
tar -xzf "$WORK/my-contest-repo/artifacts/activity-stage-20261104/app-source.tar.gz" -C "$DST/apps/examples"
python3 "$WORK/tools/patch_history_cache.py" "$APP/velaride_ride_session.c"
python3 "$WORK/tools/patch_sensor_lock.py" "$APP/velaride_ride_session.c"
cmp "$BASE/vendor/sifli/boards/sf32lb52/drivers/lcd/drv_lcd.c" "$DST/vendor/sifli/boards/sf32lb52/drivers/lcd/drv_lcd.c"
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/configure.log" 2>&1 || { tail -30 "$SAVE/configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/build.log" 2>&1 || { grep -n -E "error|Error" "$SAVE/build.log" | head -30; exit 1; }
cmp "$WORK/my-contest-repo/artifacts/activity-stage-20261104/config" "$OUT/.config" && echo CONFIG-SAME
grep -n "velaride_ride_session.c.*warning" "$SAVE/build.log" | head || true
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/firmware.bin"; cp "$OUT/.config" "$SAVE/config"
tar -czf "$SAVE/app-source.tar.gz" -C "$DST/apps/examples" velaride
stat -c '%s bytes' "$OUT/nuttx.bin"; sha256sum "$OUT/nuttx.bin"
