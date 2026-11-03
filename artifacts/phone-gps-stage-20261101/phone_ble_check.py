"""Check the companion's real GATT protocol; synthetic GPS is cleared after testing."""
import argparse
import asyncio
import codecs
import json
from pathlib import Path
import time

from bleak import BleakClient, BleakScanner

SERVICE = '0000fff0-0000-1000-8000-00805f9b34fb'
TX = '0000fff1-0000-1000-8000-00805f9b34fb'
RX = '0000fff2-0000-1000-8000-00805f9b34fb'


async def check(log, expiry=False):
    device = await BleakScanner.find_device_by_filter(
        lambda device, adv: adv.local_name == 'VelaRide', timeout=20)
    if device is None:
        raise TimeoutError('VelaRide advertisement not found.')
    events = []
    saved = None
    for cycle in range(2):
        print(f'BLE check cycle {cycle + 1}: connecting', flush=True)
        async with BleakClient(device, timeout=20, services=[SERVICE],
                               winrt={'use_cached_services': False}) as client:
            queue = asyncio.Queue()
            pending = ''
            decoder = codecs.getincrementaldecoder('utf-8')()

            def receive(sender, data):
                nonlocal pending
                pending += decoder.decode(data)
                if len(pending) > 4096:
                    raise ValueError('Oversized BLE response.')
                while '\n' in pending:
                    line, pending = pending.split('\n', 1)
                    if line.strip():
                        queue.put_nowait(json.loads(line))

            await client.start_notify(TX, receive)

            async def send(command, expected=None, ok=True, **kwargs):
                frame = (json.dumps({'cmd': command, **kwargs}) + '\n').encode()
                for offset in range(0, len(frame), 18):
                    await asyncio.wait_for(client.write_gatt_char(RX, frame[offset:offset+18], response=True), 5)
                reply = await asyncio.wait_for(queue.get(), 12)
                events.append({'cycle': cycle + 1, 'command': command, 'reply': reply})
                assert reply.get('ok') is ok, (command, reply)
                if ok:
                    assert reply.get('event') == (expected or command), reply
                return reply

            await send('ping', 'pong')
            state = await send('ride_status')
            assert not state['active'], 'Do not inject synthetic GPS during a real ride.'
            record = await send('ride_latest')
            if saved is None:
                saved = record
            else:
                assert record == saved, 'Saved ride changed during validation.'
            print(f'Cycle {cycle + 1} initial phone state: {state["phone_valid"]}, {state["speed_kmh"]}, {state["distance_km"]}', flush=True)
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text(json.dumps({'passed': False, 'events': events}, indent=2), encoding='utf-8')
            assert state['phone_valid'] is False, state
            if cycle == 0:
                await send('time_sync', epoch=int(time.time()), timezone_min=480)
                await send('phone_data', speed=22.5, distance=1.234)
                state = await send('ride_status')
                assert state['phone_valid'] is True and state['speed_kmh'] == 22.5 and state['distance_km'] == 1.234, state
                if expiry:
                    await asyncio.sleep(16)
                    state = await send('ride_status')
                    assert not state['phone_valid'] and state['speed_kmh'] == 0 and state['distance_km'] == 0, state
                    await send('phone_data', speed=0, distance=1.234)
                    state = await send('ride_status')
                    assert state['phone_valid'] and state['speed_kmh'] == 0 and state['distance_km'] == 1.234, state
                await send('phone_clear')
                state = await send('ride_status')
                assert not state['phone_valid'] and state['speed_kmh'] == 0 and state['distance_km'] == 0, state
                bad = await send('phone_data', ok=False, speed=1e100, distance=0)
                assert bad['error'] == 'bad_phone_data', bad
                await send('ping', 'pong')
                await send('phone_data', speed=8, distance=0.1)
                await client.stop_notify(TX)
                await client.start_notify(TX, receive)
                state = await send('ride_status')
                assert not state['phone_valid'] and state['speed_kmh'] == 0, state
                await send('phone_data', speed=8, distance=0.1)
                # Disconnect while data is valid; the next session must not
                # revive it. No saved ride or remark is changed by this test.
            else:
                await send('phone_clear')
            await client.stop_notify(TX)
        print(f'BLE check cycle {cycle + 1}: disconnected', flush=True)
        await asyncio.sleep(1)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(json.dumps({'synthetic_gps_test': True, 'expiry_test': expiry, 'passed': True, 'events': events}, indent=2), encoding='utf-8')
    print('PASS: actual BLE connect, clock sync, GPS write/readback/clear, invalid input, disconnect and clean reconnect.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--expiry', action='store_true', help='Also verify 15-second watch-side expiry.')
    args = parser.parse_args()
    asyncio.run(check(args.log, args.expiry))
