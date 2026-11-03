#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
BASE=/home/wyj/velaride_night_stage
DST=/home/wyj/velaride_phone_gps_stage
SAVE=${VELARIDE_GPS_SAVE:-$WORK/backup/phone-gps-ui-20261101}
APP=apps/examples/velaride
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
mkdir -p "$SAVE/before"
if [ ! -f "$SAVE/before/night-app-source.tar.gz" ]; then
  tar -czf "$SAVE/before/night-app-source.tar.gz" -C "$BASE" "$APP"
fi
if [ ! -e "$DST/.phone-gps-source-copied" ]; then
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.phone-gps-source-copied"
fi
python3 - "$BASE/$APP" "$DST/$APP" "$WORK/my-contest-repo/app/velaride" "$SAVE" <<'PY'
from pathlib import Path
import difflib,sys
base,dst,canonical,save=map(Path,sys.argv[1:])
patch=[]
for name in ('velaride_ble.c','velaride_ride_session.c','velaride_sport_ui.c'):
    old=(base/name).read_text()
    new=(canonical/name).read_text()
    if name == 'velaride_ble.c':
        # Port only subscription reset and phone command handling. Historical
        # radio diagnostics in the canonical tree must not enter this stage.
        result=old
        for start,end in [('static void velaride_ccc_changed(', 'static ssize_t velaride_command_written('),
                          ('static void velaride_handle_json(', 'static ssize_t velaride_command_written(')]:
            a=result.index(start); b=result.index(end,a)
            c=new.index(start); d=new.index(end,c)
            result=result[:a]+new[c:d]+result[b:]
        new=result
    elif name == 'velaride_sport_ui.c':
        start='  if (g_sport.info == VELARIDE_INFO_CONNECTION)'
        end='  else if (g_sport.info == VELARIDE_INFO_DEVICE)'
        a=old.index(start); b=old.index(end,a)
        c=new.index(start); d=new.index(end,c)
        new=old[:a]+new[c:d]+old[b:]
    (dst/name).write_text(new)
    patch.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(save/'phone-gps.patch').write_text(''.join(patch))
PY
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/02-configure.log" 2>&1 ||
  { tail -50 "$SAVE/02-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/03-build.log" 2>&1 ||
  { tail -70 "$SAVE/03-build.log"; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/phone-gps.bin"
cp "$OUT/.config" "$SAVE/phone-gps.config"
diff -u "$BASE/cmake_out/lckfb_huangshan_pi/.config" "$OUT/.config" > "$SAVE/04-config.diff" || true
stat -c '%s bytes' "$OUT/nuttx.bin"
sha256sum "$OUT/nuttx.bin"
