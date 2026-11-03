import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location('boot_check', Path(__file__).parents[2] / 'scripts/boot_check.py')
boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot)

TOUCH = 'ls /dev/input0\r\n /dev/input0\r\nnsh> \x1b[K'
PROCS = '  15 15 100 RR Task - Ready ffff 0040856 velaride\r\n'
WAKE = 'VELARIDE_WAKE: off=1 raise_pref=1 sleep=1 err=0 idle=21078ms\r\n'


class BootCheckTest(unittest.TestCase):
    def health(self, touch=TOUCH, procs=PROCS, wake=WAKE, extra='', mounts=None):
        return boot.health_result(touch, procs, wake, touch + procs + wake + extra, mounts)

    def test_good_health(self):
        self.assertTrue(self.health()['passed'])

    def test_command_echo_is_not_input_device(self):
        for reply in ('ls /dev/input0\nnsh> ', 'ls /dev/input0\nNo such file\nnsh> '):
            self.assertFalse(self.health(touch=reply)['passed'])

    def test_ble_or_worker_is_not_ui_task(self):
        for reply in (PROCS.replace('velaride', 'velaride_ble'), PROCS.replace('Task', 'pthread'), ''):
            self.assertFalse(self.health(procs=reply)['passed'])

    def test_wake_requires_valid_status(self):
        for reply in ('', WAKE.replace('err=0', 'err=-5'), WAKE.replace('21078ms', '4294967295ms'),
                      WAKE.replace('sleep=1', 'sleep=0'), WAKE.replace('raise_pref=1', 'raise_pref=0')):
            self.assertFalse(self.health(wake=reply)['passed'])

    def test_touch_error_even_with_registered_device(self):
        self.assertFalse(self.health(extra='ft6146 initialize failed\n')['passed'])
        self.assertTrue(self.health(extra='ft6146 initialized\nother module failed\n')['passed'])

    def test_mount_directory_not_enough(self):
        self.assertFalse(self.health(mounts=' /data type tmpfs\n /data/tf\n')['passed'])
        self.assertFalse(self.health(mounts=' /data/tf-other type vfat\n')['passed'])
        self.assertTrue(self.health(mounts=' /data/tf type vfat\n')['passed'])

    def test_completion_and_failure(self):
        report = dict(error=None, requested_cycles=2, cycles=[], idle=None, ble=None)
        self.assertFalse(boot.overall_passed(report))
        report['cycles'] = [{'passed': True}]
        self.assertFalse(boot.overall_passed(report))
        report['cycles'].append({'passed': True})
        self.assertTrue(boot.overall_passed(report))
        report['idle'] = {'passed': False}
        self.assertFalse(boot.overall_passed(report))
        report['idle']['passed'] = True
        report['error'] = 'interrupted'
        self.assertFalse(boot.overall_passed(report))

    def test_ble_replies_are_matched(self):
        events = [{'command': cmd, 'reply': {'ok': True, 'event': event}}
                  for cmd, event in [('ping', 'pong'), ('ride_status', 'ride_status'), ('ride_latest', 'ride_latest')]]
        self.assertTrue(boot.ble_result(events))
        self.assertFalse(boot.ble_result(events[:2]))
        events[-1]['reply']['event'] = 'pong'
        self.assertFalse(boot.ble_result(events))

    def test_serial_open_failure_is_persisted_and_nonzero(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'failed.json'
            args = boot.parse_args(['--log', str(log)])
            with patch('builtins.print'):
                code = boot.run(args, serial_factory=lambda **kwargs: (_ for _ in ()).throw(OSError('port missing')))
            self.assertEqual(code, 1)
            report = json.loads(log.read_text(encoding='utf-8'))
            self.assertFalse(report['passed'])
            self.assertEqual(report['cycles'], [])
            self.assertIn('port missing', report['error'])

    def test_archived_five_cycle_evidence(self):
        path = Path(__file__).parents[3] / 'backup/stage1-20261027/26-boost-reset-x5.console.log'
        if not path.exists():
            self.skipTest('archived hardware log unavailable')
        for cycle in path.read_text(encoding='utf-8').split('===== CYCLE ')[1:]:
            touch = cycle.split('COMMAND: ls /dev/input0\n', 1)[1].split('COMMAND:', 1)[0]
            procs = cycle.split('COMMAND: ps\n', 1)[1].split('COMMAND:', 1)[0]
            wake = cycle.split('COMMAND: velaride wake-status\n', 1)[1]
            self.assertTrue(boot.health_result(touch, procs, wake, cycle)['passed'])


if __name__ == '__main__':
    unittest.main()
