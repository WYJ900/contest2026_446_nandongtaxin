"""Compile the real BLE snprintf expressions, including worst-case payloads."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
code = (repo / "app/velaride/velaride_ble.c").read_text()
size = re.search(r"char response\[(\d+)\];", code).group(1)
formats = []
for command in ("ride_status", "ride_latest"):
    block = code.split(f'strcmp(cmd, "{command}") == 0', 1)[1]
    formats.append(re.search(r"snprintf\(response, sizeof\(response\),.*?\);", block, re.S).group())
header = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "velaride_ride_session.h"
int main(void) {
  struct velaride_ride_snapshot_s ride = {0};
  struct velaride_ride_record_s record = {0};
  struct velaride_ride_gps_s gps = {0};
  bool phone_valid = true;
  double phone_speed = 200, phone_distance = 100000;
  int ret = 0;
  const char *cmd = "ride_latest";
  ride.active = true; ride.alert = VELARIDE_ALERT_IMPACT;
  ride.elapsed_s = ride.moving_s = ride.still_s = UINT32_MAX;
  ride.pause_count = ride.impact_count = ride.rest_count = UINT16_MAX;
  ride.safety.help = UINT16_MAX; ride.safety_remaining_s = 30;
  ride.safety.last_action = VELARIDE_SAFETY_HELP;
  record.sequence = record.elapsed_s = record.moving_s = record.still_s = UINT32_MAX;
  record.pause_count = record.impact_count = record.rest_count = UINT16_MAX;
  record.gps.time_ms = record.gps.distance_m = UINT32_MAX;
  record.gps.avg_speed_centi = record.gps.max_speed_centi = 20000;
  record.safety.version = 1;
  record.safety.checks = record.safety.help = record.safety.unanswered = UINT16_MAX;
  record.safety.last_action = VELARIDE_SAFETY_HELP;
  record.safety.last_elapsed_s = UINT32_MAX;
  gps = record.gps;
'''
source = header + f"char response[{size}];\n"
for statement in formats:
    # Inspect snprintf's return value rather than accepting a truncated frame.
    source += "{ int length = " + statement + "\nassert(length > 0 && length < sizeof(response)); puts(response); }\n"
source += "record.safety = (struct velaride_ride_safety_s){0};\n" + formats[1] + "\nputs(response); return 0; }\n"
with tempfile.TemporaryDirectory(prefix="velaride-ble-safety-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
                    "-I" + str(repo / "app/velaride"), str(path / "test.c"), "-o", str(path / "test")], check=True)
    messages = [json.loads(line) for line in subprocess.check_output([str(path / "test")], text=True).splitlines()]
status, latest, legacy = messages
assert status["safety_pending"] and status["safety_remaining_s"] == 30
assert status["ride_distance_m"] == 0xffffffff
assert latest["safety_version"] == 1 and latest["safety_help"] == 65535
assert latest["safety_last_elapsed_s"] == 0xffffffff and latest["gps_max_centi"] == 20000
assert legacy["safety_version"] == 0 and legacy["safety_checks"] == 0
print("PASS: actual BLE ride_status/ride_latest formatting, worst-case size, GPS preservation and legacy safety fields")
