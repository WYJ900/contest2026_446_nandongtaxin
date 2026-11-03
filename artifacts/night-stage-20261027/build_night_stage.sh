#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
BASE=/home/wyj/velaride_volume_stage
DST=/home/wyj/velaride_night_stage
SAVE=$WORK/backup/night-stage-20261027
APP=apps/examples/velaride
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
if [ ! -e "$DST/.night-source-copied" ]; then
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.night-source-copied"
fi
python3 - "$BASE/$APP" "$DST/$APP" <<'PY'
from pathlib import Path
import sys
base, dst = map(Path, sys.argv[1:])
for name in ('velaride_shell.c', 'velaride_sport_ui.c', 'velaride_sport_ui.h', 'velaride_alarm.c', 'velaride_countdown.c', 'velaride_icons.h', 'CMakeLists.txt', 'Kconfig'):
    (dst / name).write_text((base / name).read_text())
    for suffix in ('.rej', '.orig'):
        (dst / (name + suffix)).unlink(missing_ok=True)
PY
patch --directory="$DST/$APP" -p1 --fuzz=0 < "$SAVE/night.patch" > "$SAVE/02-apply.log"
for file in velaride_night.c velaride_night.h velaride_night_ui.c velaride_night_ui.h velaride_font_night_28.c; do
  cp "$WORK/my-contest-repo/app/velaride/$file" "$DST/$APP/"
done
# Only the new app option is added to the archived board configuration.
python3 - "$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
option = 'CONFIG_EXAMPLES_VELARIDE_NIGHT=y'
if option not in s:
    p.write_text(s.rstrip() + '\n' + option + '\n')
PY
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/03-configure.log" 2>&1 ||
  { tail -50 "$SAVE/03-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/04-build.log" 2>&1 ||
  { tail -70 "$SAVE/04-build.log"; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/night.bin"
cp "$OUT/.config" "$SAVE/night.config"
diff -u "$BASE/cmake_out/lckfb_huangshan_pi/.config" "$OUT/.config" > "$SAVE/05-config.diff" || true
stat -c '%s bytes' "$OUT/nuttx.bin"
sha256sum "$OUT/nuttx.bin"
