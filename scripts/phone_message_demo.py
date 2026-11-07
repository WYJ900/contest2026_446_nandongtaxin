"""Send bounded, synthetic phone notifications to the real VelaRide over BLE.

No firmware changes, saved-ride changes, or outgoing phone messages.
Weather is explicitly demonstration data, not a live forecast.
"""
import argparse
import asyncio
import codecs
from datetime import datetime
import json
from pathlib import Path
import time
import uuid

from bleak import BleakClient, BleakScanner

SERVICE = '0000fff0-0000-1000-8000-00805f9b34fb'
TX = '0000fff1-0000-1000-8000-00805f9b34fb'
RX = '0000fff2-0000-1000-8000-00805f9b34fb'
MESSAGES = [
    ('微信', '骑行群', '我到公园门口了，路上注意安全。'),
    ('微信', '家人', '骑车注意安全，到家说一声。'),
    ('微信', '骑行群', '前面路口等你，一起出发。'),
]


async def demo(args, events):
    print('电脑模拟手机，通过真实蓝牙发送；天气为演示数据。', flush=True)
    device = await BleakScanner.find_device_by_filter(
        lambda device, adv: adv.local_name == 'VelaRide', timeout=15)
    if device is None:
        raise TimeoutError('未找到 VelaRide，请确认手表已开机且未连接手机。')
    async with BleakClient(device, timeout=20, services=[SERVICE],
                           winrt={'use_cached_services': False}) as client:
        queue = asyncio.Queue()
        decoder = codecs.getincrementaldecoder('utf-8')()
        pending = ''

        def receive(sender, data):
            nonlocal pending
            pending += decoder.decode(data)
            if len(pending) > 4096:
                queue.put_nowait(ValueError('BLE response too large'))
                pending = ''
                return
            while '\n' in pending:
                line, pending = pending.split('\n', 1)
                if line.strip():
                    try:
                        queue.put_nowait(json.loads(line))
                    except ValueError as error:
                        queue.put_nowait(error)

        await client.start_notify(TX, receive)

        async def send(command):
            frame = (json.dumps(command, ensure_ascii=False) + '\n').encode('utf-8')
            for offset in range(0, len(frame), 18):
                await asyncio.wait_for(client.write_gatt_char(
                    RX, frame[offset:offset + 18], response=True), 8)
            expected = 'pong' if command['cmd'] == 'ping' else command['cmd']
            deadline = time.monotonic() + 12
            while True:
                reply = await asyncio.wait_for(queue.get(), max(.01, deadline - time.monotonic()))
                if isinstance(reply, Exception):
                    raise reply
                events.append({'at': datetime.now().isoformat(timespec='seconds'),
                               'command': command, 'reply': reply})
                if not reply.get('ok'):
                    raise RuntimeError(f'手表拒绝命令: {reply}')
                if reply.get('event') == expected:
                    break
            print(f'手表已确认: {expected}', flush=True)

        await send({'cmd': 'ping'})
        await send({'cmd': 'time_sync', 'epoch': int(time.time()), 'timezone_min': 480})
        await send({'cmd': 'phone_weather', 'city': '演示', 'description': '晴',
                    'temperature_deci': 230, 'published_epoch': int(time.time())})
        print(f'天气已就绪，{args.delay:g} 秒后发第一条消息，请开始录像。', flush=True)
        await asyncio.sleep(args.delay)
        for index, (source, title, body) in enumerate(MESSAGES[:args.count]):
            if index:
                await asyncio.sleep(args.interval)
            print(f'发送 {index + 1}/{args.count}: {source} / {title} / {body}', flush=True)
            await send({'cmd': 'phone_note', 'id': uuid.uuid4().hex,
                        'source': source, 'title': title, 'body': body})
        print('发送结束。消息和天气留在手表供拍摄；点横幅或进入 设置 → 手机消息。', flush=True)
        # Give the last banner time to be filmed before the link-down banner.
        await asyncio.sleep(8)
        await client.stop_notify(TX)


def bounded_seconds(raw):
    value = float(raw)
    if not 0 <= value <= 60:
        raise argparse.ArgumentTypeError('请输入 0 到 60 秒')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--count', type=int, choices=range(1, 4), default=3)
    parser.add_argument('--delay', type=bounded_seconds, default=10)
    parser.add_argument('--interval', type=bounded_seconds, default=15)
    parser.add_argument('--log', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'backup' /
                        'phone-demo-20261107' / f'demo-{datetime.now():%Y%m%d-%H%M%S}.json')
    args = parser.parse_args()
    report = {'synthetic_phone_demo': True, 'synthetic_weather': True,
              'transport': 'real BLE', 'events': [], 'passed': False}
    try:
        asyncio.run(demo(args, report['events']))
        report['passed'] = True
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
