#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
BASE=/home/wyj/velaride_ride_gps_stage
DST=/home/wyj/velaride_safety_stage
SAVE=$WORK/backup/safety-stage-20261103
APP=apps/examples/velaride
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
mkdir -p "$SAVE"
test "$(sha256sum "$BASE/cmake_out/lckfb_huangshan_pi/nuttx.bin" | cut -d' ' -f1)" = \
  9041c538529b46f3419c06f46bf70cfdd2570c0d20a58f704b31f9b147f7c0ef
if [ ! -e "$DST/.safety-source-copied" ]; then
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.safety-source-copied"
fi
python3 - "$BASE/$APP" "$DST/$APP" "$WORK/my-contest-repo" "$SAVE" <<'PY'
from pathlib import Path
import difflib,subprocess,sys
base,dst,repo,save=map(Path,sys.argv[1:])
patch=[]
for name in ('velaride_ble.c','velaride_ride_session.c','velaride_ride_session.h','velaride_sport_ui.c'):
    # Restore only the files owned by this feature in the isolated new tree.
    (dst/name).write_text((base/name).read_text())
    old=subprocess.check_output(['git','-C',str(repo),'show',
        'checkpoint-before-safety-20261103:app/velaride/'+name]).decode().replace('\r\n','\n')
    new=(repo/'app/velaride'/name).read_text()
    patch.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),
        fromfile='a/'+name,tofile='b/'+name))
(save/'safety.patch').write_text(''.join(patch))
PY
patch --directory="$DST/$APP" -p1 --fuzz=0 < "$SAVE/safety.patch" > "$SAVE/02-apply.log"
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/03-configure.log" 2>&1 ||
  { tail -50 "$SAVE/03-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/04-build.log" 2>&1 ||
  { tail -65 "$SAVE/04-build.log"; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/safety.bin"
cp "$OUT/.config" "$SAVE/safety.config"
diff -u "$BASE/cmake_out/lckfb_huangshan_pi/.config" "$OUT/.config" > "$SAVE/05-config.diff"
stat -c '%s bytes' "$OUT/nuttx.bin"
sha256sum "$OUT/nuttx.bin"
