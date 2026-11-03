import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { validateRide } from '../public/ride-data.js';

const source = readFileSync(new URL('../public/app.js', import.meta.url), 'utf8')
  .replace("import { validateRide } from './ride-data.js';", '')
  .replace("import { RideBleTransport } from './ble-transport.js';", '');
const ids = [...readFileSync(new URL('../public/index.html', import.meta.url), 'utf8')
  .matchAll(/id="([^"]+)"/g)].map((match) => match[1]);

function page(reviewFetch = async () => { throw new Error('offline'); }) {
  const hooks = {};
  const elements = Object.fromEntries(ids.map((id) => [id, {
    value: '', textContent: '', disabled: false, validity: { badInput: false },
    listeners: {}, focus() { this.focused = true; },
    addEventListener(type, handler) { this.listeners[type] = handler; },
  }]));
  runInNewContext(source + '\n__testHooks.receive = handleMessage;', {
    __testHooks: hooks,
    validateRide, document: { getElementById: (id) => {
      assert.ok(elements[id], `Missing HTML element: ${id}`);
      return elements[id];
    } },
    navigator: {}, TextEncoder, TextDecoder, AbortController, AbortSignal,
    RideBleTransport: class { connected = false; disconnect() {} },
    setTimeout, clearTimeout, setInterval: () => {},
    fetch: (url, options) => url === '/api/health'
      ? Promise.resolve({ ok: true, json: async () => ({ ai: 'fallback' }) })
      : reviewFetch(url, options),
  });
  elements.receive = hooks.receive;
  return elements;
}

const click = (elements, id) => elements[id].listeners.click();
const response = (review) => ({ ok: true, json: async () => ({
  ok: true, review, source: 'fallback', fallback_reason: 'upstream_error',
}) });

test('读取真实 GPS 和安全结果用于复盘，手工编辑和样例清除旧扩展字段', async () => {
  const sent = [];
  const ui = page(async (url, options) => { sent.push(JSON.parse(options.body)); return response('复盘'); });
  ui.receive({ ok:true, event:'ride_latest', sequence:12, elapsed_s:100, moving_s:50, still_s:10,
    impacts:2, pause_count:1, gps_time_ms:10000, gps_distance_m:50, gps_avg_centi:1800, gps_max_centi:2000,
    safety_version:1, safety_checks:2, safety_confirmed:1, safety_pauses:0, safety_help:1,
    safety_unanswered:1, safety_last_action:3, safety_last_elapsed_s:90 });
  await click(ui,'generateButton');
  assert.equal(sent[0].distance_km,0.05);
  assert.equal(sent[0].safety_help,1);
  assert.equal(sent[0].gps_avg_centi,1800);
  ui.elapsedInput.value='200'; ui.elapsedInput.listeners.input();
  await click(ui,'generateButton');
  assert.equal(sent[1].safety_help,undefined);
  assert.equal(sent[1].gps_time_ms,undefined);
  click(ui,'sampleButton');
  await click(ui,'generateButton');
  assert.equal(sent[2].safety_version,undefined);
});

test('真实录入保留用户数据，历史样例单独加载并标记', () => {
  const ui = page();
  ui.elapsedInput.value = '120';
  click(ui, 'demoButton');
  assert.equal(ui.elapsedInput.value, '120');
  assert.match(ui.connectionBadge.textContent, /未连接/);
  assert.equal(ui.elapsedInput.focused, true);
  click(ui, 'sampleButton');
  assert.equal(Number(ui.elapsedInput.value), 8);
  assert.equal(ui.rideState.textContent, '历史样例');
});

test('无效输入不发送请求，错误显示在复盘区域且按钮恢复', async () => {
  let calls = 0;
  const ui = page(async () => { calls++; });
  await click(ui, 'generateButton');
  assert.equal(calls, 0);
  assert.match(ui.aiSource.textContent, /生成失败.*总时长/);
  assert.equal(ui.generateButton.disabled, false);
});

test('网络失败清除旧结果并退出分析中状态', async () => {
  const ui = page();
  ui.elapsedInput.value = '8';
  ui.reviewText.value = '旧复盘';
  await click(ui, 'generateButton');
  assert.equal(ui.reviewText.value, '');
  assert.match(ui.aiSource.textContent, /生成失败.*offline/);
  assert.equal(ui.generateButton.disabled, false);
});

test('编辑记录取消旧请求，较晚返回的旧复盘不能覆盖新复盘', async () => {
  const requests = [];
  const ui = page((url, options) => new Promise((resolve) => {
    requests.push({ resolve, signal: options.signal });
  }));
  ui.elapsedInput.value = '8';
  const first = click(ui, 'generateButton');
  // Repeated clicks cannot start duplicate cloud requests.
  await click(ui, 'generateButton');
  assert.equal(requests.length, 1);
  ui.elapsedInput.value = '20';
  ui.elapsedInput.listeners.input();
  assert.equal(requests[0].signal.aborted, true);
  assert.equal(ui.elapsedValue.textContent, '20 秒');
  const second = click(ui, 'generateButton');
  requests[1].resolve(response('新的复盘'));
  await second;
  requests[0].resolve(response('过期复盘'));
  await first;
  assert.equal(ui.reviewText.value, '新的复盘');
  assert.match(ui.aiSource.textContent, /AI 服务暂不可用/);
  assert.equal(ui.generateButton.disabled, false);
});
