# Live demo view: merges agent.log (console chat), emulator_console.log
# (heartbeat + tool trace) and watch_serial.log (what the real watch did)
# into one readable Chinese timeline for screen recording. Read-only.
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '..', '..'))
AGENT_LOG = os.path.join(HERE, 'agent.log')
EMU_LOG = os.path.join(HERE, 'emulator_console.log')
WATCH_LOG = os.path.join(TOP, 'backup', 'phone-realme-20261106', 'watch_serial.log')

TOOLS = {
    'vr__ride_status': '读取骑行状态',
    'vr__ride_latest': '读取最近一次骑行记录',
    'vr__watch_chime': '让手表响铃提醒',
    'vr__ride_pause': '暂停骑行',
    'vr__ride_resume': '继续骑行',
    'vr__review_write': '把复盘写到手表',
    'read_file': '读文件（Skill / 记忆）',
    'write_file': '写记忆',
    'edit_file': '写记忆',
    'get_current_time': '读当前时间',
}
PLACEHOLDER = re.compile(r'^(正在|稍等|马上|让我|思考|处理中)')

os.system('')  # enable ANSI colours in the Windows console
C = {'you': '96', 'agent': '97', 'hb': '93', 'tool': '95', 'watch': '92', 'dim': '90'}


def show(kind, label, text):
    stamp = time.strftime('%H:%M:%S')
    print(f'\x1b[90m{stamp}\x1b[0m \x1b[{C[kind]}m{label:<8}\x1b[0m {text}', flush=True)


class Tail:
    def __init__(self, path):
        self.path = path
        self.pos = os.path.getsize(path) if os.path.exists(path) else 0
        self.buf = b''

    def lines(self):
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return []
        if size < self.pos:  # file recreated (emulator restart)
            self.pos, self.buf = 0, b''
        if size == self.pos:
            return []
        with open(self.path, 'rb') as f:
            f.seek(self.pos)
            data = f.read()
        self.pos += len(data)
        self.buf += data
        *done, self.buf = self.buf.split(b'\n')
        return [d.decode('utf-8', 'replace').rstrip('\r') for d in done]


def agent_line(line, state):
    text = re.sub(r'^\[\d\d:\d\d:\d\d\] ', '', line)
    if text.startswith('>> ask '):
        show('you', '[你]', text[7:])
    elif text.startswith('[Agent]: '):
        reply = text[9:]
        state['reply'] = not PLACEHOLDER.match(reply)
        if state['reply']:
            show('agent', '[Agent]', reply)
    elif text.startswith('vela>'):
        state['reply'] = False
    elif state.get('reply') and text.strip():
        show('agent', '', text)


def emu_line(line):
    if '[heartbeat] Triggered agent check' in line:
        show('dim', '[心跳]', '定时触发，Agent 开始主动检查')
        return
    m = re.search(r'iter=\d+ tool=(\S+)', line)
    if m and m.group(1) != '(none)':
        name = m.group(1)
        kind = 'tool' if name.startswith('vr__') else 'dim'
        show(kind, '[调用]', TOOLS.get(name, name) + (f'  ({name})' if name.startswith('vr__') else ''))
        return
    m = re.search(r'System message \[heartbeat\]: (.*)', line)
    if m:
        msg = m.group(1).strip()
        if msg == 'HEARTBEAT_OK':
            show('dim', '[心跳]', '检查完毕，现在不需要提醒')
        elif msg and not PLACEHOLDER.match(msg):
            show('hb', '[主动提醒]', msg)


def watch_line(line):
    if 'VELARIDE_RIDE: start' in line:
        show('watch', '[手表]', '开始骑行')
    elif 'VELARIDE_RIDE: pause' in line:
        show('watch', '[手表]', '已暂停 ⏸')
    elif 'VELARIDE_RIDE: resume' in line:
        show('watch', '[手表]', '已继续 ▶')
    elif 'VELARIDE_RIDE: finish' in line:
        m = re.search(r'seq=(\d+) duration=(\d+)', line)
        show('watch', '[手表]', f'骑行结束（记录 #{m.group(1)}，{m.group(2)} 秒）' if m else '骑行结束')
    elif re.search(r'VELARIDE_SOUND: cue=6 result=0', line):
        show('watch', '[手表]', '响铃 🔔')
    elif 'VELARIDE_REVIEW: transfer bytes=' in line:
        show('watch', '[手表]', '复盘已写入，可在复盘页查看')


def main():
    tails = [(Tail(AGENT_LOG), 'agent'), (Tail(EMU_LOG), 'emu'), (Tail(WATCH_LOG), 'watch')]
    state = {}
    print('\x1b[1mVelaRide 骑行教练 · ai_agent（openvela 模拟器）→ MCP → 真手表\x1b[0m', flush=True)
    while True:
        for tail, kind in tails:
            for line in tail.lines():
                if kind == 'agent':
                    agent_line(line, state)
                elif kind == 'emu':
                    emu_line(line)
                else:
                    watch_line(line)
        time.sleep(0.3)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
