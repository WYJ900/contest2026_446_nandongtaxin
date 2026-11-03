"""Bounded watch regression checks; physical touch/wake/listening remain manual."""
import argparse
import codecs
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
import time


ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
TOUCH_ERROR = re.compile(r'^.*(?:ft6146|touch).*\b(?:fail\w*|error)\b.*$', re.I | re.M)


def clean(text):
    return ANSI.sub('', text).replace('\r', '')


def health_result(touch, procs, wake, transcript, mounts=None):
    text = clean(transcript)
    wake_lines = re.findall(r'^VELARIDE_WAKE: .*$', clean(wake), re.M)
    wake_status = wake_lines[-1] if wake_lines else ''
    fields = dict(re.findall(r'(\w+)=([^\s]+)', wake_status))
    result = {
        'input0': any(line.strip() == '/dev/input0' for line in clean(touch).splitlines()),
        'velaride_task': bool(re.search(r'^\s*\d+\s+.*\bTask\s+.*\svelaride\s*$', clean(procs), re.M)),
        'nsh_prompt': 'nsh>' in clean(touch),
        'touch_error': bool(TOUCH_ERROR.search(text)),
        'wake_status': wake_status,
        'wake_ok': fields.get('err') == '0' and fields.get('off') in ('0', '1')
                   and fields.get('raise_pref') == '1' and fields.get('sleep') == '1'
                   and fields.get('idle') not in (None, '4294967295ms'),
        'sfbl': text.count('SFBL'),
    }
    if mounts is not None:
        # An empty /data/tf directory exists even when no card is mounted.
        result['tf_mounted'] = bool(re.search(r'^\s*/data/tf\s+type\s+vfat\s*$', clean(mounts), re.M))
    result['passed'] = (result['input0'] and result['velaride_task']
                        and result['nsh_prompt'] and not result['touch_error']
                        and result['wake_ok'] and result.get('tf_mounted', True))
    return result


def ble_result(events):
    replies = [e for e in events if 'command' in e]
    if [e['command'] for e in replies] != ['ping', 'ride_status', 'ride_latest']:
        return False
    return all(e.get('reply', {}).get('ok') is True and
               e['reply'].get('event') == expected
               for e, expected in zip(replies, ('pong', 'ride_status', 'ride_latest')))


def overall_passed(report):
    return (report['error'] is None
            and len(report['cycles']) == report['requested_cycles']
            and bool(report['cycles'])
            and all(c['passed'] for c in report['cycles'])
            and (report['idle'] is None or report['idle']['passed'])
            and (report['ble'] is None or report['ble']['passed']))


class Console:
    def __init__(self, port, logfile):
        self.port = port
        self.logfile = logfile
        self.decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')

    def note(self, text):
        self.logfile.write(text)
        self.logfile.flush()

    def capture(self, seconds):
        chunks = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = self.port.read(self.port.in_waiting or 1)
            if data:
                text = self.decoder.decode(data)
                chunks.append(text)
                self.note(text)
        return ''.join(chunks)

    def command(self, line, seconds=1.5):
        self.note(f'\nCOMMAND: {line}\n')
        self.port.write((line + '\r\n').encode('utf-8'))
        return self.capture(seconds)

    def health(self, check_tf):
        prompt = self.command('', 1)
        if 'vela>' in prompt:
            self.note('\nCOMMAND: <Ctrl-D>\n')
            self.port.write(b'\x04')
            prompt += self.capture(1.5)
        touch = self.command('ls /dev/input0')
        procs = self.command('ps', 2)
        wake = self.command('velaride wake-status')
        mounts = self.command('mount') if check_tf else None
        later = self.capture(1)
        transcript = prompt + touch + procs + wake + (mounts or '') + later
        return health_result(touch, procs, wake, transcript, mounts)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM5')
    parser.add_argument('--cycles', type=int, default=5)
    parser.add_argument('--boot-wait', type=float, default=25)
    parser.add_argument('--idle-seconds', type=int, default=0,
                        help='passively monitor after final reset, up to 3600 seconds')
    parser.add_argument('--ble', action='store_true', help='check legacy ping/status/latest after serial tests')
    parser.add_argument('--check-tf', action='store_true', help='require an actual vfat mount at /data/tf')
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args(argv)
    if not 1 <= args.cycles <= 20:
        parser.error('--cycles must be between 1 and 20')
    if not 10 <= args.boot_wait <= 60:
        parser.error('--boot-wait must be between 10 and 60 seconds')
    if not 0 <= args.idle_seconds <= 3600:
        parser.error('--idle-seconds must be between 0 and 3600')
    return args


def run(args, serial_factory=None):
    report = {'schema_version': 2, 'started_at': datetime.now(timezone.utc).isoformat(),
              'requested_cycles': args.cycles, 'cycles': [], 'idle': None,
              'ble': None, 'error': None, 'passed': False,
              'scope': 'Automated checks only; no cold power cycle or physical UI/audio acceptance.',
              'requested': {'port': args.port, 'boot_wait': args.boot_wait,
                            'idle_seconds': args.idle_seconds, 'ble': args.ble, 'check_tf': args.check_tf}}
    args.log.parent.mkdir(parents=True, exist_ok=True)

    def save():
        args.log.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    # Persist an explicit incomplete result before serial import/open can fail.
    save()
    port = None
    try:
        if serial_factory is None:
            import serial
            serial_factory = serial.Serial
        port = serial_factory(port=None, baudrate=1000000, timeout=0.05, write_timeout=1)
        port.dtr = False
        port.rts = False
        port.port = args.port
        with args.log.with_suffix('.console.log').open('w', encoding='utf-8') as logfile:
            console = Console(port, logfile)
            port.open()
            console.capture(0.2)
            for cycle in range(1, args.cycles + 1):
                console.note(f'\n===== CYCLE {cycle} RESET =====\n')
                port.rts = True
                time.sleep(0.3)
                port.rts = False
                boot = console.capture(args.boot_wait)
                health = console.health(args.check_tf)
                result = dict(health, cycle=cycle, sfbl_during_boot=boot.count('SFBL'),
                              sfbl_after_boot=health['sfbl'], app_banner='AI Agent starting' in boot)
                result['touch_error'] = result['touch_error'] or bool(TOUCH_ERROR.search(clean(boot)))
                result['passed'] = (health['passed'] and not result['touch_error']
                                    and result['sfbl_during_boot'] == 1 and result['sfbl_after_boot'] == 0)
                report['cycles'].append(result)
                print(json.dumps(result, ensure_ascii=False), flush=True)
                save()
                if not result['passed']:
                    raise RuntimeError(f'Cycle {cycle} failed; stopping before further tests')

            if args.idle_seconds:
                idle = {'requested_seconds': args.idle_seconds, 'observed_seconds': 0,
                        'sfbl': 0, 'passed': False}
                report['idle'] = idle
                save()
                console.note('\n===== PASSIVE IDLE (NO COMMANDS) =====\n')
                started = time.monotonic()
                idle_parts = []
                while time.monotonic() - started < args.idle_seconds:
                    idle_parts.append(console.capture(min(30, args.idle_seconds - (time.monotonic() - started))))
                    idle['observed_seconds'] = round(time.monotonic() - started, 2)
                    idle['sfbl'] = ''.join(idle_parts).count('SFBL')
                    print(f'IDLE {idle["observed_seconds"]}/{args.idle_seconds}s SFBL={idle["sfbl"]}', flush=True)
                    save()
                    if idle['sfbl']:
                        raise RuntimeError('Unexpected reboot during idle monitoring')
                health = console.health(args.check_tf)
                idle['health'] = health
                idle['sfbl'] += health['sfbl']
                idle['passed'] = (health['passed'] and idle['sfbl'] == 0
                                  and not TOUCH_ERROR.search(clean(''.join(idle_parts))))
                save()
                if not idle['passed']:
                    raise RuntimeError('Post-idle health check failed')
        port.close()
        port = None
        if args.ble:
            report['ble'] = {'passed': False}
            save()
            ble_log = args.log.with_suffix(f'.ble-{time.time_ns()}.json')
            result = subprocess.run([sys.executable, '-X', 'utf8', str(Path(__file__).with_name('ble_probe.py')),
                                     '--log', str(ble_log)], capture_output=True, text=True,
                                    encoding='utf-8', timeout=120)
            events = json.loads(ble_log.read_text(encoding='utf-8')) if ble_log.exists() else []
            report['ble'] = {'passed': result.returncode == 0 and ble_result(events),
                             'events': events, 'returncode': result.returncode,
                             'stderr': result.stderr, 'log': str(ble_log)}
    except (Exception, KeyboardInterrupt) as error:
        report['error'] = f'{type(error).__name__}: {error}'
    finally:
        if port is not None:
            try:
                port.close()
            except Exception as error:
                report['error'] = report['error'] or f'Close failed: {error}'
        report['finished_at'] = datetime.now(timezone.utc).isoformat()
        report['passed'] = overall_passed(report)
        save()
    print('PASSED' if report['passed'] else f'FAILED: {report["error"] or "BLE check failed"}', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(run(parse_args()))
