import assert from 'node:assert/strict';
import test from 'node:test';
import { RideBleTransport } from '../public/ble-transport.js';

function mockDevice(onCommand) {
  const tx = new EventTarget();
  tx.startNotifications = async () => {};
  let buffered = '';
  const decoder = new TextDecoder();
  const rx = { writeValueWithResponse: async (chunk) => {
    assert.ok(chunk.length <= 18);
    buffered += decoder.decode(chunk, { stream: true });
    if (buffered.includes('\n')) {
      const command = JSON.parse(buffered.trim());
      buffered = '';
      await onCommand(command, notify);
    }
  } };
  const device = new EventTarget();
  device.gatt = {
    connected: false,
    connect: async () => {
      device.gatt.connected = true;
      return { getPrimaryService: async () => ({ getCharacteristic: async (uuid) => uuid.includes('fff1') ? tx : rx }) };
    },
    disconnect: () => {
      device.gatt.connected = false;
      device.dispatchEvent(new Event('gattserverdisconnected'));
    },
  };
  function notify(message) {
    const bytes = new TextEncoder().encode(JSON.stringify(message) + '\n');
    // Deliberately split inside UTF-8 codepoints.
    for (let offset = 0; offset < bytes.length; offset += 2) {
      tx.value = new DataView(bytes.slice(offset, offset + 2).buffer);
      tx.dispatchEvent(new Event('characteristicvaluechanged'));
    }
  }
  return { device, notify };
}

test('BLE 中文跨分片可还原，连续请求必须等待各自确认', async () => {
  const commands = [];
  const { device } = mockDevice(async (command, notify) => {
    commands.push(command);
    await new Promise(setImmediate);
    notify({ ok: true, event: command.cmd === 'ping' ? 'pong' : command.cmd, text: '中文复盘' });
  });
  const client = new RideBleTransport();
  await client.connect(device);
  const replies = await Promise.all([client.request({ cmd: 'ping' }),
    client.request({ cmd: 'ride_review', text: '本次骑行完成，请定时休息。' })]);
  assert.deepEqual(replies.map((reply) => reply.text), ['中文复盘', '中文复盘']);
  assert.equal(commands[1].text, '本次骑行完成，请定时休息。');
  client.disconnect();
});

test('设备返回失败不会被当作回传成功', async () => {
  const { device } = mockDevice((command, notify) => notify({ ok: false, error: 'bad_review' }));
  const client = new RideBleTransport();
  await client.connect(device);
  await assert.rejects(client.request({ cmd: 'ride_review', text: '' }), /bad_review/);
  client.disconnect();
});

test('ATT 写入挂起仍受总超时约束，超时断开以免串错回复', async () => {
  const { device } = mockDevice(() => new Promise(() => {}));
  const client = new RideBleTransport({ timeoutMs: 20 });
  await client.connect(device);
  await assert.rejects(client.request({ cmd: 'ping' }), /超时/);
  assert.equal(client.connected, false);
});

test('断开会拒绝未完成请求，重新连接可恢复', async () => {
  let respond = false;
  const { device } = mockDevice((command, notify) => { if (respond) notify({ ok: true, event: 'pong' }); });
  const client = new RideBleTransport();
  await client.connect(device);
  const pending = client.request({ cmd: 'ping' });
  await new Promise(setImmediate);
  device.gatt.disconnect();
  await assert.rejects(pending, /断开/);
  respond = true;
  await client.connect(device);
  assert.equal((await client.request({ cmd: 'ping' })).event, 'pong');
  client.disconnect();
});
