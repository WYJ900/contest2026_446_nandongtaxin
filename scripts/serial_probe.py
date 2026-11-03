"""Bounded NSH diagnostics; open with DTR/RTS disabled, reset only on request."""
import argparse
import codecs
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM5')
parser.add_argument('--command', action='append', default=[])
parser.add_argument('--commands-file', type=Path)
parser.add_argument('--wait', type=float, default=2)
parser.add_argument('--reset', action='store_true')
parser.add_argument('--log', type=Path)
args = parser.parse_args()
if not 0 < args.wait <= 45:
    parser.error('--wait must be between 0 and 45 seconds')
output = []
decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')

def emit(text):
    output.append(text)
    print(text, end='', flush=True)

def capture(port, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data = port.read(port.in_waiting or 1)
        if data:
            emit(decoder.decode(data))

port = serial.Serial(port=None, baudrate=1000000, timeout=0.05, write_timeout=1)
port.dtr = False
port.rts = False
port.port = args.port
try:
    port.open()
    if args.reset:
        port.rts = True
        time.sleep(0.3)
        port.rts = False
        capture(port, 8)
    else:
        capture(port, 0.2)
    commands = args.commands_file.read_text(encoding='utf-8').splitlines() if args.commands_file else args.command
    for command in commands:
        if '\r' in command or '\n' in command:
            raise ValueError('One NSH command per --command argument')
        emit(f'\nCOMMAND: {command}\n')
        port.write((command + '\r\n').encode('utf-8'))
        capture(port, args.wait)
finally:
    port.close()
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(''.join(output), encoding='utf-8')
