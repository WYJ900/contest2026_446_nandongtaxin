"""Exercise the Android 0.1.1 basic handshake against a real watch.

Only phone_clear (when supported) and RTC time_sync can change volatile state.
No GPS samples, ride mutations, review writes, audio or cloud requests are sent.
This is a PC BLE protocol test, not Android UI/permission acceptance.
"""
import argparse
import asyncio
import codecs
from datetime import datetime, timezone
import json
from pathlib import Path
import time

SERVICE = '0000fff0-0000-1000-8000-00805f9b34fb'
TX = '0000fff1-0000-1000-8000-00805f9b34fb'
RX = '0000fff2-0000-1000-8000-00805f9b34fb'


def expect(reply, event):
    if reply.get('ok') is not True or reply.get('event') != event:
        raise RuntimeError(f'Expected {event}, received {reply}')
    return reply


async def check(report, save):
    from bleak import BleakClient, BleakScanner
    device = await BleakScanner.find_device_by_filter(
        lambda device, adv: adv.local_name == 'VelaRide', timeout=20)
    if device is None:
        raise TimeoutError('VelaRide advertisement not found')
    saved = None
    for cycle in range(1, 3):
        async with BleakClient(device, timeout=20, services=[SERVICE],
                               winrt={'use_cached_services': False}) as client:
            queue = asyncio.Queue()
            decoder = codecs.getincrementaldecoder('utf-8')()
            pending = ''

            def receive(sender, data):
                nonlocal pending
                try:
                    pending += decoder.decode(data)
                    if len(pending) > 4096:
                        raise ValueError('Oversized notification')
                    while '\n' in pending:
                        line, pending = pending.split('\n', 1)
                        if line.strip():
                            queue.put_nowait(json.loads(line))
                except Exception as error:
                    queue.put_nowait(error)

            await client.start_notify(TX, receive)

            async def send(command, **values):
                frame = (json.dumps({'cmd': command, **values}) + '\n').encode()
                for offset in range(0, len(frame), 18):
                    await asyncio.wait_for(client.write_gatt_char(RX, frame[offset:offset + 18], response=True), 5)
                reply = await asyncio.wait_for(queue.get(), 12)
                if isinstance(reply, Exception):
                    raise reply
                report['events'].append({'cycle': cycle, 'command': command, 'reply': reply})
                save()
                print(json.dumps(report['events'][-1], ensure_ascii=False), flush=True)
                return reply

            expect(await send('ping'), 'pong')
            reply = await send('phone_clear')
            if reply.get('ok') is False and reply.get('error') == 'unknown_cmd':
                gps_supported = False
            else:
                expect(reply, 'phone_clear')
                gps_supported = True
            report['gps_supported'] = gps_supported
            epoch = int(time.time())
            reply = expect(await send('time_sync', epoch=epoch, timezone_min=480), 'time_sync')
            if reply.get('epoch') != epoch:
                raise RuntimeError('Clock acknowledgement differs from requested epoch')
            status = expect(await send('ride_status'), 'ride_status')
            if gps_supported and status.get('phone_valid') is not False:
                raise RuntimeError('GPS extension did not clear old data')
            record = expect(await send('ride_latest'), 'ride_latest')
            if saved is None:
                saved = record
            elif record != saved:
                raise RuntimeError('Saved ride changed between connections')
            await client.stop_notify(TX)
        report['completed_cycles'] = cycle
        save()
    report['passed'] = report['completed_cycles'] == 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'error': None, 'completed_cycles': 0, 'events': [],
              'scope': 'PC BLE protocol; Android phone not tested',
              'started_at': datetime.now(timezone.utc).isoformat()}

    def save():
        args.log.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    save()
    try:
        asyncio.run(check(report, save))
    except (Exception, KeyboardInterrupt) as error:
        report['passed'] = False
        report['error'] = f'{type(error).__name__}: {error}'
    finally:
        report['finished_at'] = datetime.now(timezone.utc).isoformat()
        save()
    print('PASSED' if report['passed'] else f'FAILED: {report["error"]}', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
