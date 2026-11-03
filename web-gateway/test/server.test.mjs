import assert from 'node:assert/strict';
import test, { before, after } from 'node:test';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { createAppServer, fallbackReview } from '../server.mjs';
import { validateRide } from '../public/ride-data.js';

// Tests must never use credentials or device settings from the host machine.
const envKeys = ['MIMO_API_KEY', 'ANTHROPIC_AUTH_TOKEN', 'ANTHROPIC_BASE_URL',
  'ANTHROPIC_MODEL', 'VELARIDE_ENABLE_DEVICE_BRIDGE', 'AI_PROTOCOL', 'AI_BASE_URL',
  'AI_MODEL', 'AI_API_KEY', 'AI_PROVIDER_NAME', 'AI_AUTH', 'AI_TIMEOUT_MS'];
const savedEnv = Object.fromEntries(envKeys.map((key) => [key, process.env[key]]));
before(() => envKeys.forEach((key) => { delete process.env[key]; }));
after(() => envKeys.forEach((key) => {
  if (savedEnv[key] === undefined) delete process.env[key];
  else process.env[key] = savedEnv[key];
}));

async function appFor(t) {
  const app = createAppServer().listen(0, '127.0.0.1');
  await once(app, 'listening');
  t.after(() => app.close());
  return `http://127.0.0.1:${app.address().port}`;
}

const post = (url, body) => fetch(url, {
  method: 'POST', headers: { 'content-type': 'application/json' },
  body: JSON.stringify(body),
});

test('短骑行复盘保留秒数，不把八秒夸大成一分钟', () => {
  assert.match(fallbackReview({ elapsed_s: 8, moving_s: 5 }), /骑行 8 秒/);
  assert.match(fallbackReview({ elapsed_s: 61 }), /骑行 1 分钟 1 秒/);
});

test('复盘保留 GPS 与安全处理依据，拒绝缺字段和不一致的统计', () => {
  const ride = {elapsed_s:100,moving_s:50,still_s:10,gps_time_ms:10000,gps_distance_m:50,
    gps_avg_centi:1800,gps_max_centi:2000,safety_version:1,safety_checks:2,safety_confirmed:1,
    safety_pauses:0,safety_help:1,safety_unanswered:1,safety_last_action:3,safety_last_elapsed_s:90};
  const checked=validateRide(ride);
  assert.equal(checked.distance_km,0.05);
  assert.equal(checked.safety_help,1);
  assert.match(fallbackReview(ride),/不表示已发送消息/);
  assert.match(fallbackReview(ride),/未及时确认/);
  for(const bad of [{...ride,distance_km:1},{...ride,gps_avg_centi:20001},
      {...ride,safety_confirmed:2},{...ride,safety_version:0},{...ride,safety_last_elapsed_s:101},
      {elapsed_s:10,safety_checks:1},{elapsed_s:10,gps_time_ms:10}]) assert.throws(()=>validateRide(bad));
});

test('提醒处理和目标字段保留，不能把提醒次数当成已经补水', () => {
  const record={elapsed_s:300,rest_count:2,extras_version:1,goal_distance_m:5000,lap_count:2,
    rest_taken:1,rest_snoozed:1,rest_ignored:0,drink_count:2,drink_done:0,drink_snoozed:1,drink_ignored:1};
  assert.equal(validateRide(record).rest_snoozed,1);
  assert.match(fallbackReview(record),/确认补水 0 次/);
  assert.throws(()=>validateRide({...record,drink_done:1}));
  assert.throws(()=>validateRide({...record,rest_count:1}));
  assert.throws(()=>validateRide({...record,extras_version:0}));
});

test('校验拒绝错误类型、负数、小数计数和不一致的时间', () => {
  for (const value of [null, [], 'ride', {}, { elapsed_s: -1 },
    { elapsed_s: '8' }, { elapsed_s: 8, impacts: 0.5 },
    { elapsed_s: 8, moving_s: 9 }, { elapsed_s: 8, still_s: -1 },
    { elapsed_s: 8, distance_km: Infinity }, { elapsed_s: 8, impacts: true },
    { elapsed_s: 8, moving_s: 6, still_s: 3 }]) {
    assert.throws(() => validateRide(value));
  }
  assert.deepEqual(validateRide({ elapsed_s: 8, moving_s: 5 }), {
    elapsed_s: 8, moving_s: 5, still_s: 0, pause_count: 0, impacts: 0, distance_km: 0,
  });
});

test('review API 拒绝无效记录并返回可读错误', async (t) => {
  const url = await appFor(t);
  for (const record of [null, [], { elapsed_s: 0 }, { elapsed_s: 8, moving_s: 9 }]) {
    const response = await post(`${url}/api/ride/review`, record);
    assert.equal(response.status, 400);
    const data = await response.json();
    assert.equal(data.ok, false);
    assert.equal(typeof data.error, 'string');
  }
});

test('设备接口默认关闭，拒绝请求而不执行串口桥接', async (t) => {
  const url = await appFor(t);
  const response = await post(`${url}/api/device`, { action: 'ride_status' });
  assert.equal(response.status, 403);
  assert.match((await response.json()).error, /停用/);
});

test('MiMo 接口失败时返回区别于未配置的降级原因', async (t) => {
  const mimo = createServer((req, res) => res.writeHead(503).end()).listen(0, '127.0.0.1');
  await once(mimo, 'listening');
  t.after(() => mimo.close());
  process.env.ANTHROPIC_AUTH_TOKEN = 'test-token';
  process.env.ANTHROPIC_BASE_URL = `http://127.0.0.1:${mimo.address().port}`;
  t.after(() => {
    delete process.env.ANTHROPIC_AUTH_TOKEN;
    delete process.env.ANTHROPIC_BASE_URL;
  });
  const url = await appFor(t);
  const response = await post(`${url}/api/ride/review`, { elapsed_s: 8 });
  const data = await response.json();
  assert.equal(response.status, 200);
  assert.equal(data.source, 'fallback');
  assert.equal(data.fallback_reason, 'upstream_error');
  assert.match(data.review, /8 秒/);
});

test('fallbackReview 生成简短中文复盘', () => {
  const text = fallbackReview({ elapsed_s: 1200, moving_s: 900, impacts: 1 });
  assert.match(text, /骑行 20 分钟/);
  assert.match(text, /1 次冲击/);
});

test('health 和 review API 可用', async (t) => {
  const server = createAppServer().listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(() => server.close());
  const { port } = server.address();

  const health = await fetch(`http://127.0.0.1:${port}/api/health`).then((res) => res.json());
  assert.equal(health.ok, true);

  const response = await fetch(`http://127.0.0.1:${port}/api/ride/review`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ elapsed_s: 600, moving_s: 500, impacts: 0 }),
  });
  const body = await response.json();
  assert.equal(response.status, 200);
  assert.equal(body.ok, true);
  assert.equal(body.source, 'fallback');
  assert.equal(body.fallback_reason, 'not_configured');
  assert.ok(body.review.length > 10);
});

test('Anthropic-compatible MiMo 请求使用 Bearer Token', async (t) => {
  let capturedAuth = '';
  let capturedModel = '';
  let capturedThinking;
  const mimo = createServer(async (req, res) => {
    capturedAuth = req.headers.authorization || '';
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    const request = JSON.parse(Buffer.concat(chunks).toString());
    capturedModel = request.model;
    capturedThinking = request.thinking;
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({
      content: [
        { type: 'thinking', thinking: 'test-only' },
        { type: 'text', text: '本次骑行结束，无撞击。起步前注意路况。' },
      ],
    }));
  }).listen(0, '127.0.0.1');
  await once(mimo, 'listening');
  t.after(() => mimo.close());

  const oldToken = process.env.ANTHROPIC_AUTH_TOKEN;
  const oldBase = process.env.ANTHROPIC_BASE_URL;
  const oldModel = process.env.ANTHROPIC_MODEL;
  process.env.ANTHROPIC_AUTH_TOKEN = 'test-token';
  process.env.ANTHROPIC_BASE_URL = `http://127.0.0.1:${mimo.address().port}`;
  process.env.ANTHROPIC_MODEL = 'mimo-v2.5';
  t.after(() => {
    if (oldToken === undefined) delete process.env.ANTHROPIC_AUTH_TOKEN;
    else process.env.ANTHROPIC_AUTH_TOKEN = oldToken;
    if (oldBase === undefined) delete process.env.ANTHROPIC_BASE_URL;
    else process.env.ANTHROPIC_BASE_URL = oldBase;
    if (oldModel === undefined) delete process.env.ANTHROPIC_MODEL;
    else process.env.ANTHROPIC_MODEL = oldModel;
  });

  const app = createAppServer().listen(0, '127.0.0.1');
  await once(app, 'listening');
  t.after(() => app.close());
  const response = await fetch(`http://127.0.0.1:${app.address().port}/api/ride/review`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ elapsed_s: 600, moving_s: 500 }),
  });
  const body = await response.json();
  assert.equal(body.source, 'mimo-anthropic');
  assert.equal(body.watch_source, 'mimo-anthropic');
  assert.equal(body.watch_review, body.review);
  assert.ok(body.watch_review.length > 0);
  assert.equal(capturedAuth, 'Bearer test-token');
  assert.equal(capturedModel, 'mimo-v2.5');
  assert.deepEqual(capturedThinking, { type: 'disabled' });
});

test('字库外字符可由同一 AI 改写，网页和手表保留相同的最终正文', async (t) => {
  let calls = 0;
  const mock = createServer(async (req, res) => {
    for await (const chunk of req) { /* Drain the complete request. */ }
    calls++;
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ content: [{ type: 'text',
      text: calls === 1 ? '骑行结束🚲，请休息。' : '骑行结束，请休息。' }] }));
  }).listen(0, '127.0.0.1');
  await once(mock, 'listening');
  t.after(() => mock.close());
  process.env.ANTHROPIC_AUTH_TOKEN = 'test-token';
  process.env.ANTHROPIC_BASE_URL = `http://127.0.0.1:${mock.address().port}`;
  t.after(() => {
    delete process.env.ANTHROPIC_AUTH_TOKEN;
    delete process.env.ANTHROPIC_BASE_URL;
  });
  const url = await appFor(t);
  const response = await post(`${url}/api/ride/review`, { elapsed_s: 25 });
  const data = await response.json();
  assert.equal(calls, 2);
  assert.equal(data.adapted_for_watch, true);
  assert.equal(data.review, '骑行结束，请休息。');
  assert.equal(data.watch_review, data.review);
  assert.equal(data.watch_source, data.source);
  assert.match(data.original_review, /🚲/);
});
