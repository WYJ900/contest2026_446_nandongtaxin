#!/bin/bash
set -euo pipefail
WORK=/mnt/d/desktop/VelaRide_AI
F=/tmp/velaride-history-cache-test
rm -rf "$F"; mkdir -p "$F/app" "$F/tests"
cp -r /home/wyj/velaride_history_cache/apps/examples/velaride "$F/app/velaride"
cp -r "$WORK/my-contest-repo/tests/ride_session" "$F/tests/"
python3 "$WORK/tools/patch_history_cache_test.py" "$F/tests/ride_session/test.c"
bash "$F/tests/ride_session/run.sh"
