import test from 'node:test';
import assert from 'node:assert/strict';
import { appendFile, mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createWatchLink, handleMcp, watchSafeText } from '../watch-mcp.mjs';

// Imitates serial_logger.py: echoes each new command line into the log
// after some unrelated BLE noise, then prints the watch's reply.
async function fakeLogger(dir, replies) {
  const cmdFile = join(dir, 'cmd.txt');
  const logFile = join(dir, 'log.txt');
  await writeFile(cmdFile, '');
  await writeFile(logFile, '[17:00:00] boot\n');
  let seen = 0;
  const timer = setInterval(async () => {
    const lines = (await readFile(cmdFile, 'utf8')).split('\n').filter(Boolean);
    for (; seen < lines.length; seen++) {
      const cmd = lines[seen];
      const reply = replies(cmd);
      await appendFile(logFile, `[17:00:01] >> ${cmd}\n[17:00:01] \x1b[0mVELARIDE_BT_HW: noise\n[17:00:01] ${cmd}\n${reply.map((r) => `[17:00:01] ${r}\n`).join('')}`);
    }
  }, 20);
  return { cmdFile, logFile, stop: () => clearInterval(timer) };
}

test('MCP handshake and tool listing match the ai_agent client', async () => {
  const link = {};
  const init = await handleMcp(link, { jsonrpc: '2.0', id: 1, method: 'initialize', params: {} });
  assert.match(init.result.protocolVersion, /^2025-/);
  const list = await handleMcp(link, { jsonrpc: '2.0', id: 2, method: 'tools/list' });
  assert.deepEqual(list.result.tools.map((t) => t.name),
    ['ride_status', 'ride_latest', 'ride_pause', 'ride_resume', 'watch_chime', 'review_write']);
  assert.equal(await handleMcp(link, { jsonrpc: '2.0', method: 'notifications/initialized' }), null);
  const missing = await handleMcp(link, { jsonrpc: '2.0', id: 3, method: 'tools/call', params: { name: 'format_flash' } });
  assert.equal(missing.error.code, -32602);
});

test('tool calls go through the logger files and parse the watch reply', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'vr-mcp-'));
  const parts = [];
  const logger = await fakeLogger(dir, (cmd) => {
    if (cmd === 'velaride ride-status') return ['{"active":true,"paused":false,"moving_s":130}'];
    if (cmd === 'velaride ride-pause') return ['{"ok":true,"paused":true}'];
    if (cmd === 'velaride review-begin') return ['VELARIDE_REVIEW: transfer begin'];
    if (cmd.startsWith('velaride review-part ')) { parts.push(cmd.slice(21)); return [`VELARIDE_REVIEW: part=${cmd.slice(21).length / 2} total=0`]; }
    if (cmd === 'velaride review-end') return ['VELARIDE_REVIEW: transfer bytes=10 persist=0'];
    return ['unknown'];
  });
  try {
    const link = createWatchLink({ cmdFile: logger.cmdFile, logFile: logger.logFile, timeoutMs: 2000, pollMs: 20 });
    const status = await handleMcp(link, { jsonrpc: '2.0', id: 1, method: 'tools/call', params: { name: 'ride_status' } });
    assert.equal(JSON.parse(status.result.content[0].text).moving_s, 130);
    const pause = await handleMcp(link, { jsonrpc: '2.0', id: 2, method: 'tools/call', params: { name: 'ride_pause' } });
    assert.equal(JSON.parse(pause.result.content[0].text).paused, true);
    const text = '骑行两分钟，记得休息。Good ride!';
    const review = await handleMcp(link, { jsonrpc: '2.0', id: 3, method: 'tools/call', params: { name: 'review_write', arguments: { text } } });
    assert.equal(review.result.isError, undefined);
    assert.equal(Buffer.from(parts.join(''), 'hex').toString('utf8'), watchSafeText(text));
    assert.ok(parts.every((p) => p.length <= 64), 'review parts stay within 32 bytes');
  } finally { logger.stop(); }
});

test('a silent watch becomes a tool error, not a hang', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'vr-mcp-'));
  const logger = await fakeLogger(dir, () => []);
  try {
    const link = createWatchLink({ cmdFile: logger.cmdFile, logFile: logger.logFile, timeoutMs: 300, pollMs: 20 });
    const out = await handleMcp(link, { jsonrpc: '2.0', id: 1, method: 'tools/call', params: { name: 'ride_latest' } });
    assert.equal(out.result.isError, true);
    assert.match(JSON.parse(out.result.content[0].text).error, /手表无回复/);
  } finally { logger.stop(); }
});

test('watch text keeps ASCII and font glyphs within 260 bytes', () => {
  assert.equal(watchSafeText('  abc  '), 'abc');
  assert.ok(Buffer.byteLength(watchSafeText('骑'.repeat(200))) <= 260);
});

test('watch_chime reports ok for the real queued-status line', async () => {
  const line = 'enabled=1 voice=1 rings=1 volume=100 dnd=0 active=0 pending=1 saving=0 save_error=0 error=0 completed=5 cancelled=1 skipped=0 hw=0 ret=0';
  const call = (status) => handleMcp({ chime: async () => status },
    { jsonrpc: '2.0', id: 1, method: 'tools/call', params: { name: 'watch_chime' } });
  assert.equal(JSON.parse((await call(line)).result.content[0].text).ok, true);
  assert.equal(JSON.parse((await call(line.replace('ret=0', 'ret=-16'))).result.content[0].text).ok, false);
});
