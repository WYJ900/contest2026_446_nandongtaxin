#!/bin/bash
# Out-of-tree goldfish-arm64 + ai_agent build; does not touch lckfb_huangshan_pi output.
set -e
[ -d $HOME/velaride_agent_emu/nuttx ] || { date +%T; rsync -a --exclude=/cmake_out $HOME/velaride_history_cache/ $HOME/velaride_agent_emu/; echo copied; }
TC=$HOME/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin
export PATH=$TC:/home/wyj/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
SRC=$HOME/velaride_agent_emu
CFG=$HOME/agent_emu_cfg/goldfish-arm64-v8a-ap
OUT=$SRC/cmake_out/vela_goldfish-arm64-v8a-ap
LOG=$HOME/agent_emu_cfg/logs
mkdir -p "$CFG" "$LOG"
[ -f "$CFG/defconfig" ] || cp "$SRC/packages/ai_agent/defconfigs/goldfish-arm64-v8a-ap/goldfish-arm64-v8a-ap_defconfig" "$CFG/defconfig"
aarch64-none-elf-gcc --version | head -1
cd "$SRC/nuttx"
date +%T
cmake -B "$OUT" -S "$SRC/nuttx" -GNinja -DBOARD_CONFIG="$CFG" \
  '-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations -Wno-error=unused-function' > "$LOG/configure.log" 2>&1 || { tail -40 "$LOG/configure.log"; exit 1; }
echo configured; date +%T
cmake --build "$OUT" -- -j8 > "$LOG/build.log" 2>&1 || { grep -n -E "error|Error" "$LOG/build.log" | head -40; tail -20 "$LOG/build.log"; exit 1; }
date +%T
grep -E "^CONFIG_(EXAMPLES_AI_AGENT_VELA|FEATURE_SYSTEM_VELACLAW|NET)=" "$OUT/.config"
ls -la "$OUT"/nuttx* "$OUT"/vela_* 2>/dev/null | head
