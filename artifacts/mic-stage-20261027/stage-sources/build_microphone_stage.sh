#!/usr/bin/env bash
set -euo pipefail
BASE=/home/wyj/velaride_rc17_baseline
DST=/home/wyj/velaride_microphone_stage
WORK=/mnt/d/desktop/VelaRide_AI
SAVE=$WORK/backup/restore-20261026
if [ ! -f "$BASE/cmake_out/lckfb_huangshan_pi/nuttx.bin" ]; then
  echo 'Reconstructed baseline must build successfully first.'; exit 1
fi
if [ ! -e "$DST/.microphone-stage-ready" ]; then
  mkdir -p "$DST"
  rsync -a --exclude=/cmake_out "$BASE/" "$DST/"
  touch "$DST/.microphone-stage-ready"
fi
SRC=$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src
cp "$WORK/build/stages/microphone-only/"*.c "$SRC/"
cp "$WORK/build/stages/microphone-only/audio_test.h" "$SRC/"
cp "$BASE/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/CMakeLists.txt" "$SRC/CMakeLists.txt"
cat >> "$SRC/CMakeLists.txt" <<'CMAKE'

if(CONFIG_EXAMPLES_VELARIDE)
  nuttx_add_application(NAME mic_test STACKSIZE 4096 PRIORITY 100
    SRCS mic_test.c audio_test_lock.c)
  target_link_libraries(apps_mic_test PRIVATE sifli_hal)
endif()
CMAKE
# Check that every original app source remains byte-identical to the baseline.
diff -qr "$BASE/apps/examples/velaride" "$DST/apps/examples/velaride"
export PATH=/opt/arm-gnu-toolchain/bin:$HOME/.local/bin:$PATH
OUT=$DST/cmake_out/lckfb_huangshan_pi
cd "$DST/nuttx"
cmake -B "$OUT" -S "$DST/nuttx" -GNinja \
  -DBOARD_CONFIG="$DST/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations' > "$SAVE/microphone-stage-configure.log" 2>&1 ||
  { tail -30 "$SAVE/microphone-stage-configure.log"; exit 1; }
cmake --build "$OUT" -- -j8 > "$SAVE/microphone-stage-build.log" 2>&1 ||
  { tail -40 "$SAVE/microphone-stage-build.log"; exit 1; }
test "$(stat -c %s "$OUT/nuttx.bin")" -lt $((0x990000))
cp "$OUT/nuttx.bin" "$SAVE/rc17-microphone-only.bin"
cp "$OUT/.config" "$SAVE/rc17-microphone-only.config"
sha256sum "$SAVE/rc17-microphone-only.bin"
