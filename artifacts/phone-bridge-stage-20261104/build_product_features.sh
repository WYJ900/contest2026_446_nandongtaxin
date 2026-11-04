#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
BASE=/home/wyj/velaride_safety_stage
DST=/home/wyj/velaride_product_features
NAME=${1:?stage name required}
REF=${2:?parent Git commit required}
case "$NAME" in *[!a-z0-9-]*|'') echo 'Invalid stage name'; exit 1;; esac
STAMP=${3:-20261103}
SAVE=$WORK/backup/$NAME-$STAMP
APP=apps/examples/velaride
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
mkdir -p "$SAVE"
if [ ! -e "$DST/.features-source-copied" ]; then
  test "$(sha256sum "$BASE/cmake_out/lckfb_huangshan_pi/nuttx.bin" | cut -d' ' -f1)" = \
    9308cce7e800daea6c5bd512306d2c3271b013b7d930c7980d8be27f2383c410
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.features-source-copied"
fi
python3 - "$DST/$APP" "$WORK/my-contest-repo" "$SAVE" "$REF" <<'PY'
from pathlib import Path
import difflib,subprocess,sys
dst,repo,save=map(Path,sys.argv[1:4]); ref=sys.argv[4]
before=save/'before'; before.mkdir(exist_ok=True)
patch=[]
names=('velaride_ble.c','velaride_ride_session.c','velaride_ride_session.h','velaride_sport_ui.c',
       'CMakeLists.txt','velaride_memo.c','velaride_memo.h','velaride_activity.c','velaride_activity.h',
       'velaride_device.c','velaride_shell.c','velaride_sport_ui.h','velaride_font_night_28.c',
       'velaride_phone.c','velaride_phone.h')
for name in names:
    if not (repo/'app/velaride'/name).exists(): continue
    if not (before/name).exists(): (before/name).write_bytes((dst/name).read_bytes() if (dst/name).exists() else b'')
    (dst/name).write_text((before/name).read_text())
    result=subprocess.run(['git','-C',str(repo),'show',ref+':app/velaride/'+name],capture_output=True)
    old=result.stdout.decode().replace('\r\n','\n') if result.returncode==0 else ''
    new=(repo/'app/velaride'/name).read_text()
    patch.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(save/'feature.patch').write_text(''.join(patch))
PY
patch --directory="$DST/$APP" -p1 --fuzz=0 < "$SAVE/feature.patch" > "$SAVE/01-apply.log"
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/02-configure.log" 2>&1 ||
  { tail -40 "$SAVE/02-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/03-build.log" 2>&1 ||
  { tail -55 "$SAVE/03-build.log"; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/firmware.bin"
cp "$OUT/.config" "$SAVE/config"
diff -u "$BASE/cmake_out/lckfb_huangshan_pi/.config" "$OUT/.config" > "$SAVE/config.diff"
tar -czf "$SAVE/app-source.tar.gz" -C "$DST/apps/examples" velaride
stat -c '%s bytes' "$OUT/nuttx.bin"
sha256sum "$OUT/nuttx.bin"
