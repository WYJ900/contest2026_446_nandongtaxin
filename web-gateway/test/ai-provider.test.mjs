import assert from 'node:assert/strict';
import test from 'node:test';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { existsSync, readFileSync } from 'node:fs';
import { WATCH_FONT_CHARACTERS } from '../watch-font.mjs';
import { resolveConfig, endpointFor, publicConfig, requestReview, clipUtf8, extractText } from '../ai-provider.mjs';

test('兼容根地址、版本路径、自定义前缀及完整接口，不重复拼接 v1', () => {
  const cases = [
    ['https://relay.example', 'openai', '/v1/chat/completions'],
    ['https://relay.example/v1/', 'openai', '/v1/chat/completions'],
    ['https://relay.example/api/v1/chat/completions', 'openai', '/api/v1/chat/completions'],
    ['https://relay.example/v1beta/openai', 'openai', '/v1beta/openai/chat/completions'],
    ['https://relay.example/anthropic', 'anthropic', '/anthropic/v1/messages'],
    ['https://relay.example/v1', 'anthropic', '/v1/messages'],
    ['https://relay.example/v1/responses', 'responses', '/v1/responses'],
  ];
  for (const [base, protocol, expected] of cases) assert.equal(new URL(endpointFor(base, protocol)).pathname, expected);
  assert.throws(() => endpointFor('https://relay.example/v1/messages', 'openai'), /不匹配/);
  assert.throws(() => endpointFor('https://relay.example?key=secret', 'openai'));
});

test('显式中转配置不会借用旧 MiMo Key，公开信息不泄露凭据', () => {
  const config = resolveConfig({ AI_BASE_URL: 'https://relay.example/v1', AI_MODEL: 'custom-model',
    MIMO_API_KEY: 'old-key', ANTHROPIC_AUTH_TOKEN: 'another-key' });
  assert.equal(config.key, '');
  assert.deepEqual(publicConfig(config), {
    provider: '自定义 AI', model: 'custom-model', protocol: 'openai', configured: false,
  });
  assert.throws(() => resolveConfig({ AI_API_KEY: 'secret' }), /填写/);
  assert.throws(() => resolveConfig({ ANTHROPIC_AUTH_TOKEN: 'other-service-key' }), /完整/);
});

for (const protocol of ['openai', 'responses', 'anthropic']) {
  test(`${protocol} 真实 HTTP 模拟服务验证请求、鉴权与文本解析`, async (t) => {
    let captured;
    const mock = createServer(async (req, res) => {
      const chunks = [];
      for await (const chunk of req) chunks.push(chunk);
      captured = { path: req.url, headers: req.headers, body: JSON.parse(Buffer.concat(chunks)) };
      const replies = {
        openai: { choices: [{ message: { content: '骑行8秒。注意定时休息。' } }] },
        responses: { output: [{ type: 'reasoning', summary: [] }, { type: 'message', role: 'assistant',
          content: [{ type: 'output_text', text: '骑行8秒。注意定时休息。' }] }] },
        anthropic: { content: [{ type: 'thinking', thinking: 'not a result' },
          { type: 'text', text: '骑行8秒。' }, { type: 'text', text: '注意定时休息。' }] },
      };
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(JSON.stringify(replies[protocol]));
    }).listen(0, '127.0.0.1');
    await once(mock, 'listening');
    t.after(() => mock.close());
    const config = resolveConfig({ AI_PROTOCOL: protocol,
      AI_BASE_URL: `http://127.0.0.1:${mock.address().port}/v1`, AI_MODEL: 'relay-model',
      AI_API_KEY: 'local-test-key', AI_AUTH: protocol === 'anthropic' ? 'x-api-key' : 'bearer' });
    assert.equal(await requestReview(config, { elapsed_s: 8 }, 'system instructions'), '骑行8秒。注意定时休息。');
    assert.equal(captured.body.model, 'relay-model');
    assert.equal(captured.body.temperature, undefined);
    assert.equal(captured.body.thinking, undefined);
    const record = JSON.parse(protocol === 'responses' ? captured.body.input :
      captured.body.messages.at(-1).content);
    assert.equal(record.distance_status, 'not_recorded');
    assert.equal(Object.hasOwn(record, 'distance_km'), false);
    if (protocol === 'anthropic') {
      assert.equal(captured.path, '/v1/messages');
      assert.equal(captured.headers['x-api-key'], 'local-test-key');
      assert.equal(captured.headers.authorization, undefined);
      assert.equal(captured.body.system, 'system instructions');
    } else {
      assert.equal(captured.headers.authorization, 'Bearer local-test-key');
      assert.equal(captured.path, protocol === 'responses' ? '/v1/responses' : '/v1/chat/completions');
      if (protocol === 'responses') {
        assert.equal(captured.body.store, false);
        assert.equal(captured.body.instructions, 'system instructions');
      } else assert.equal(captured.body.messages[0].content, 'system instructions');
    }
    const fast = resolveConfig({ AI_PROTOCOL: protocol,
      AI_BASE_URL: config.baseUrl, AI_MODEL: 'deepseek-v4-flash-0731', AI_API_KEY: 'local-test-key' });
    await requestReview(fast, { elapsed_s: 8 }, 'system instructions');
    if (protocol === 'responses') assert.deepEqual(captured.body.reasoning, { effort: 'none' });
    else assert.deepEqual(captured.body.thinking, { type: 'disabled' });
    assert.equal(resolveConfig({ AI_PROTOCOL: protocol, AI_BASE_URL: config.baseUrl,
      AI_MODEL: 'deepseek-v4-flash-0731', AI_THINKING: 'provider' }).fastReview, false);
  });
}

test('空文本和思考块不能冒充复盘；UTF-8 短版不切断中文', () => {
  assert.throws(() => extractText({ content: [{ type: 'thinking', thinking: '秘密推理' }] }, 'anthropic'));
  const short = clipUtf8('骑行'.repeat(100));
  assert.ok(Buffer.byteLength(short) <= 260);
  assert.equal(Buffer.from(short).toString('utf8'), short);
  assert.equal(short.includes('\ufffd'), false);
});

test('网关声明的字符与固件生成字库一致', () => {
  const fontUrl = ['../../app/velaride/velaride_font_cn_20.c',
    '../../apps/examples/velaride/velaride_font_cn_20.c']
    .map((path) => new URL(path, import.meta.url)).find(existsSync);
  assert.ok(fontUrl, '找不到固件字库');
  const font = readFileSync(fontUrl, 'utf8');
  const cmaps = font.match(/cmaps\[\]\s*=\s*\{([\s\S]+?)\n\};/)[1];
  const glyphs = [];
  for (const map of cmaps.matchAll(/\{([^}]+)\}/g)) {
    const start = Number(map[1].match(/\.range_start\s*=\s*(\d+)/)[1]);
    const length = Number(map[1].match(/\.range_length\s*=\s*(\d+)/)[1]);
    const list = map[1].match(/\.unicode_list\s*=\s*(\w+)/)[1];
    if (list === 'NULL') {
      assert.match(map[1], /LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY/);
      glyphs.push(...Array.from({length}, (_, i) => start + i));
    } else {
      const table = font.match(new RegExp(`${list}\\[\\]\\s*=\\s*\\{([^}]+)\\}`))[1];
      glyphs.push(...[...table.matchAll(/0x([0-9a-f]+)/gi)]
        .map((match) => start + parseInt(match[1], 16)));
    }
  }
  assert.deepEqual(glyphs, [...WATCH_FONT_CHARACTERS].map((char) => char.codePointAt(0)));
});

test('HTTP 错误只返回状态，不暴露服务端错误正文', async (t) => {
  const mock = createServer((req, res) => res.writeHead(401).end('echoed-secret')).listen(0, '127.0.0.1');
  await once(mock, 'listening');
  t.after(() => mock.close());
  const config = resolveConfig({ AI_BASE_URL: `http://127.0.0.1:${mock.address().port}`,
    AI_MODEL: 'relay-model', AI_API_KEY: 'secret' });
  await assert.rejects(requestReview(config, {}, 'system'), (error) =>
    error.status === 401 && !error.message.includes('secret'));
});
