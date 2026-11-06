"""Runs ai_agent inside the emulator over `adb shell` and keeps its vela> CLI.

Lines appended to the command file are forwarded to the agent's stdin; all
agent output is timestamped into the log. Same pattern as serial_logger.py.
API keys never pass through the command file or the log: set_llm_key.ps1
sends the user's set_llm line over a loopback-only socket, it is forwarded
to the agent unlogged, and ai_agent persists it in /data/ai_agent/config.
"""
import os, socket, subprocess, sys, threading, time

adb, serial, log_path, cmd_path = sys.argv[1:5]
open(cmd_path, 'a').close()
log = open(log_path, 'a', encoding='utf-8', buffering=1)

def stamp(text):
    log.write('[%s] %s\n' % (time.strftime('%H:%M:%S'), text))

proc = subprocess.Popen([adb, '-s', serial, 'shell', 'ai_agent'],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, bufsize=0)
stamp('== ai_agent started via adb (pid %d)' % proc.pid)

def pump():
    buf = b''
    while True:
        data = proc.stdout.read1(4096) if hasattr(proc.stdout, 'read1') else proc.stdout.read(1)
        if not data:
            break
        buf += data
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            stamp(line.decode('utf-8', 'replace').rstrip('\r'))
        if buf.endswith(b'vela> '):
            stamp(buf.decode('utf-8', 'replace'))
            buf = b''
    stamp('== ai_agent exited (%s)' % proc.wait())

threading.Thread(target=pump, daemon=True).start()

def secrets():
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', 5999))
    server.listen(1)
    while True:
        conn, _ = server.accept()
        with conn:
            line = conn.makefile('r', encoding='utf-8').readline().strip()
            if line.startswith('set_llm ') and proc.poll() is None:
                stamp('>> set_llm (entered by the user, not logged)')
                proc.stdin.write((line + '\n').encode('utf-8'))
                proc.stdin.flush()
                conn.sendall(b'sent\n')
            else:
                conn.sendall(b'rejected\n')

threading.Thread(target=secrets, daemon=True).start()
pos = os.path.getsize(cmd_path)
while proc.poll() is None:
    size = os.path.getsize(cmd_path)
    if size > pos:
        with open(cmd_path, 'rb') as f:
            f.seek(pos)
            new = f.read().decode('utf-8', 'replace')
        pos = size
        for cmd in new.splitlines():
            if cmd.startswith('set_llm') or cmd.startswith('router_set'):
                stamp('>> (refused: keys must be entered by the user)')
                continue
            stamp('>> ' + cmd)
            proc.stdin.write((cmd + '\n').encode('utf-8'))
            proc.stdin.flush()
    time.sleep(0.2)
time.sleep(1)
