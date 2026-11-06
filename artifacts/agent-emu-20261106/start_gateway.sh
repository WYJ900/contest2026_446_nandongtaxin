#!/bin/bash
# Gateway for the agent demo: MCP to the real watch + Agent dashboard feed +
# phone reply endpoint. Run after serial_logger.py and agent_console.py.
B=D:/desktop/VelaRide_AI/backup
cd D:/desktop/VelaRide_AI/my-contest-repo/web-gateway || exit 1
export VELARIDE_MCP=1
export VELARIDE_SERIAL_CMD=$B/phone-realme-20261106/serial_cmd.txt
export VELARIDE_SERIAL_LOG=$B/phone-realme-20261106/watch_serial.log
export VELARIDE_AGENT_LOG=$B/agent-emu-20261106/agent.log
export VELARIDE_AGENT_CMD=$B/agent-emu-20261106/agent_cmd.txt
export VELARIDE_EMU_LOG=$B/agent-emu-20261106/emulator_console.log
export VELARIDE_RIDE_HISTORY=$B/diag-20261105/watch_serial.log
export PORT=4173
exec node server.mjs
