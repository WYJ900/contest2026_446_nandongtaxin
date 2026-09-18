#!/usr/bin/env bash
set -euo pipefail

TEAM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_ROOT="$(cd "$TEAM_ROOT/.." && pwd)"
BOARD_CONFIG="vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh"
JOBS="${JOBS:-8}"

"$TEAM_ROOT/scripts/apply-integration.sh"
cd "$WORKSPACE_ROOT"
./build.sh "$BOARD_CONFIG" "-j$JOBS"

echo "Firmware: $WORKSPACE_ROOT/nuttx/nuttx.bin"
