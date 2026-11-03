"""Bounded VelaRide GATT diagnostics. No pairing or unrelated-device output."""
import argparse
import asyncio
import codecs
import json
import time
import urllib.request
from pathlib import Path
from bleak import BleakClient, BleakScanner

SERVICE = '0000fff0-0000-1000-8000-00805f9b34fb'
TX = '0000fff1-0000-1000-8000-00805f9b34fb'
RX = '0000fff2-0000-1000-8000-00805f9b34fb'
parser = argparse.ArgumentParser()
parser.add_argument('--scan-only', action='store_true')
parser.add_argument('--cycles', type=int, default=1)
parser.add_argument('--command', action='append', default=[])
parser.add_argument('--commands-file', type=Path)
parser.add_argument('--gateway-url')
parser.add_argument('--require-ai', action='store_true')
parser.add_argument('--sync-time', action='store_true')
parser.add_argument('--log', type=Path)
args = parser.parse_args()
events = []

def report(event):
    events.append(event)
    print(json.dumps(event, ensure_ascii=False), flush=True)

async def main():
    if not 1 <= args.cycles <= 10:
        raise ValueError('cycles must be 1..10')
    commands = (json.loads(args.commands_file.read_text(encoding='utf-8'))
                if args.commands_file else
                [json.loads(raw) if raw.startswith('{') else {'cmd': raw}
                 for raw in args.command]) or [
        {'cmd': 'ping'}, {'cmd': 'ride_status'}, {'cmd': 'ride_latest'}]
    for cycle in range(args.cycles):
        device = await BleakScanner.find_device_by_filter(
            lambda device, adv: adv.local_name == 'VelaRide', timeout=15)
        if device is None:
            raise TimeoutError('VelaRide advertisement not found in 15 seconds')
        report({'event': 'found', 'name': device.name, 'cycle': cycle + 1})
        if args.scan_only:
            return
        # A firmware reboot can leave Windows with stale GATT characteristics.
        # Discover the requested service from the device for each probe.
        async with BleakClient(device, timeout=20, services=[SERVICE],
                               winrt={'use_cached_services': False}) as client:
            report({'event': 'connected', 'mtu': client.mtu_size})
            pending = ''
            decoder = codecs.getincrementaldecoder('utf-8')()
            queue = asyncio.Queue()

            def notification(sender, data):
                nonlocal pending
                pending += decoder.decode(data)
                if len(pending) > 4096:
                    raise ValueError('Oversized response')
                while '\n' in pending:
                    line, pending = pending.split('\n', 1)
                    if line.strip():
                        queue.put_nowait(json.loads(line))

            await client.start_notify(TX, notification)
            async def send(command):
                frame = (json.dumps(command, ensure_ascii=False) + '\n').encode()
                for offset in range(0, len(frame), 18):
                    await asyncio.wait_for(client.write_gatt_char(
                        RX, frame[offset:offset + 18], response=True), timeout=8)
                reply = await asyncio.wait_for(queue.get(), timeout=10)
                report({'command': command['cmd'], 'reply': reply})
                if not reply.get('ok'):
                    raise RuntimeError(f'Device rejected {command["cmd"]}: {reply}')
                return reply

            if args.sync_time:
                await send({'cmd': 'time_sync', 'epoch': int(time.time()), 'timezone_min': 480})
            for command in commands:
                await send(command)
            if args.gateway_url:
                if args.cycles != 1:
                    raise ValueError('Cloud review tests must use --cycles 1')
                record = await send({'cmd': 'ride_latest'})

                def get_review():
                    request = urllib.request.Request(
                        args.gateway_url.rstrip('/') + '/api/ride/review',
                        data=json.dumps(record).encode(),
                        headers={'Content-Type': 'application/json'}, method='POST')
                    with urllib.request.urlopen(request, timeout=65) as response:
                        return json.load(response)

                review = await asyncio.to_thread(get_review)
                report({'event': 'review_generated', 'result': review})
                if not review.get('ok') or (args.require_ai and review.get('source') == 'fallback'):
                    raise RuntimeError('A real AI result was required but unavailable')
                await send({'cmd': 'ride_review', 'text': review['watch_review'],
                            'sequence': record['sequence']})
            await client.stop_notify(TX)
        report({'event': 'disconnected', 'cycle': cycle + 1})

try:
    asyncio.run(main())
finally:
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(json.dumps(events, ensure_ascii=False, indent=2), encoding='utf-8')
