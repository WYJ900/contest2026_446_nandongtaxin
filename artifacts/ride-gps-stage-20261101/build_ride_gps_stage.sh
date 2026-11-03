#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
BASE=/home/wyj/velaride_phone_gps_stage
DST=/home/wyj/velaride_ride_gps_stage
SAVE=$WORK/backup/ride-gps-20261101
APP=apps/examples/velaride
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
if [ ! -e "$DST/.ride-gps-source-copied" ]; then
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.ride-gps-source-copied"
fi
python3 - "$BASE/$APP" "$DST/$APP" "$WORK/my-contest-repo/app/velaride" "$SAVE" <<'PY'
from pathlib import Path
import difflib,sys
base,dst,canonical,save=map(Path,sys.argv[1:])
patch=[]
for name in ('velaride_ble.c','velaride_ride_session.c','velaride_ride_session.h','velaride_sport_ui.c'):
    (dst/name).write_text((base/name).read_text())
    for suffix in ('.rej','.orig'):
        (dst/(name+suffix)).unlink(missing_ok=True)
    old=(save/'before/velaride'/name).read_text()
    new=(canonical/name).read_text()
    patch.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(save/'ride-gps.patch').write_text(''.join(patch))
PY
patch --directory="$DST/$APP" -p1 --fuzz=0 < "$SAVE/ride-gps.patch" > "$SAVE/03-apply.log"
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/04-configure.log" 2>&1 ||
  { tail -50 "$SAVE/04-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/05-build.log" 2>&1 ||
  { tail -70 "$SAVE/05-build.log"; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/ride-gps.bin"
cp "$OUT/.config" "$SAVE/ride-gps.config"
diff -u "$BASE/cmake_out/lckfb_huangshan_pi/.config" "$OUT/.config" > "$SAVE/06-config.diff" || true
stat -c '%s bytes' "$OUT/nuttx.bin"
sha256sum "$OUT/nuttx.bin"
