#!/usr/bin/env bash
# Stage 2b: accepted v16 board playback (speaker_test) on the frozen mic stage.
set -euo pipefail
BASE=/home/wyj/velaride_microphone_stage
DST=/home/wyj/velaride_speaker_stage
WORK=/mnt/d/desktop/VelaRide_AI
SAVE=$WORK/backup/stage1-20261027
REL=vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src
if [ ! -e "$DST/.speaker-stage-ready" ]; then
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out --exclude=/.microphone-stage-ready \
    --exclude="/$REL/touch_probe.c" "$BASE/" "$DST/"
  touch "$DST/.speaker-stage-ready"
fi
SRC=$DST/$REL
cp "$WORK/build/stages/speaker/speaker_test.c" "$SRC/"
cp "$WORK/build/stages/speaker/voice_test_dsp.h" "$SRC/"
cp "$BASE/$REL/CMakeLists.txt" "$SRC/CMakeLists.txt"
cat >> "$SRC/CMakeLists.txt" <<'CMAKE'

if(CONFIG_EXAMPLES_VELARIDE)
  nuttx_add_application(NAME speaker_test STACKSIZE 4096 PRIORITY 100
    SRCS speaker_test.c)
  target_link_libraries(apps_speaker_test PRIVATE sifli_hal)
endif()
CMAKE
# Only the speaker files and the appended registration may differ from the mic stage.
diff -rq --no-dereference --exclude=cmake_out --exclude=.git \
  --exclude=.microphone-stage-ready --exclude=.speaker-stage-ready \
  --exclude=touch_probe.c "$BASE" "$DST" > "$SAVE/15-speaker-stage-scope.txt" || true
cat "$SAVE/15-speaker-stage-scope.txt"
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/16-speaker-stage-configure.log" 2>&1 ||
  { tail -30 "$SAVE/16-speaker-stage-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/17-speaker-stage-build.log" 2>&1 ||
  { grep -n -E "error|undefined" "$SAVE/17-speaker-stage-build.log" | head -40; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cmp "$WORK/my-contest-repo/artifacts/mic-stage-20261027/config" "$OUT/.config" &&
  echo "config identical to frozen mic stage"
cp "$OUT/nuttx.bin" "$SAVE/mic-speaker-stage.bin"
cp "$OUT/.config" "$SAVE/mic-speaker-stage.config"
stat -c '%s bytes' "$OUT/nuttx.bin"
sha256sum "$SAVE/mic-speaker-stage.bin"
