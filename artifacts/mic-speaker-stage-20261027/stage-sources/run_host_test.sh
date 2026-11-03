#!/usr/bin/env bash
set -euo pipefail
W=/mnt/d/desktop/VelaRide_AI
cd /tmp
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$W/build/stages/speaker" -o voice_host "$W/build/stages/speaker/host_test_voice.c" -lm
./voice_host "$W/exports/voice/mic-speech-20261026.wav" 3200 \
  "$W/exports/voice/mic-stage-20261027-take2.wav" 0 \
  "$W/exports/voice/speaker-stage-20261027.wav" 0 \
  "$W/exports/voice/speaker-stage-20261027-take2.wav" 0
