import assert from 'node:assert/strict';
import test from 'node:test';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { createAppServer, fallbackReview } from '../server.mjs';

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
        { type: 'text', text: '本次骑行较稳。建议继续注意补水和定时休息。' },
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
  assert.ok(body.watch_review.length > 0);
  assert.equal(capturedAuth, 'Bearer test-token');
  assert.equal(capturedModel, 'mimo-v2.5');
  assert.deepEqual(capturedThinking, { type: 'disabled' });
});
