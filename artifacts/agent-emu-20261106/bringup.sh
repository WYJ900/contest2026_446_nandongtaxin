#!/bin/bash
# After each emulator boot: static NAT address (NETINIT leaves eth0 at 0.0.0.0 here), then show persisted agent state.
cd "$(dirname "$0")/../.."
E="timeout 20 .local-tools/android-sdk/platform-tools/adb.exe -s emulator-5554"
export MSYS_NO_PATHCONV=1
$E shell ifup eth0 > /dev/null 2>&1
$E shell ifconfig eth0 10.0.2.15 dr 10.0.2.2 netmask 255.255.255.0 dns 10.0.2.3 > /dev/null 2>&1
$E shell ifconfig eth0 2>&1 | grep "inet addr"
$E shell curl -s -m 10 http://10.0.2.2:4173/api/health 2>&1 | head -c 80; echo
$E shell ls /data/ai_agent /data/ai_agent/config 2>&1 | tr '\n' ' '; echo
$E shell ls /data/ai_agent/skills/ride-coach.md /data/ai_agent/HEARTBEAT.md 2>&1
