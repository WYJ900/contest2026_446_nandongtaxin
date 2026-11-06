import test from 'node:test';
import assert from 'node:assert/strict';
import { createFeed, parseRideRecord } from '../agent-feed.mjs';

test('ride records are taken only from real ride-latest JSON lines', () => {
  const line = '[19:29:53] {"ok":true,"sequence":20,"elapsed_s":313,"moving_s":260,"still_s":51,"pause_count":1,"impact_count":4,"rest_count":0}';
  assert.equal(parseRideRecord(line).sequence, 20);
  assert.equal(parseRideRecord('{"ok":false,"error":-2}'), null);
  assert.equal(parseRideRecord('{"active":true}'), null);
});

test('feed turns logs and MCP calls into dashboard events', () => {
  const feed = createFeed();
  feed.onAgentLine('[19:00:00] >> ask 帮我复盘');
  feed.onAgentLine('[19:00:01] [Agent]: 正在分析...');
  feed.onAgentLine('[19:00:09] [Agent]: 本次运动 4 分钟。');
  feed.onAgentLine('[19:00:09] 已写到手表复盘页。');
  feed.onEmuLine('[ 1.0] [ap] [heartbeat] Triggered agent check');
  feed.onEmuLine('[ 2.0] [ap] [trace:ab] iter=0 tool=read_file latency=1ms');
  feed.onEmuLine('[ 3.0] [ap] [agent] System message [heartbeat]: HEARTBEAT_OK');
  feed.onWatchLine('[19:09:50] VELARIDE_RIDE: finish seq=21 duration=300 moving=250 still=50 persist=0');
  feed.recordToolCall('ride_latest', {}, { content: [{ text: '{"ok":true,"sequence":21,"elapsed_s":300,"moving_s":250,"still_s":50,"pause_count":0,"impact_count":2,"rest_count":0}' }] });
  feed.recordToolCall('review_write', { text: 'x' }, { content: [{ text: '{"ok":true,"text":"比上次少 10 秒","bytes":20}' }] });
  const snap = feed.snapshot();
  assert.deepEqual(snap.events.map((e) => e.kind),
    ['user', 'agent', 'heartbeat', 'step', 'idle', 'watch', 'tool', 'tool']);
  assert.equal(snap.events[1].text, '本次运动 4 分钟。\n已写到手表复盘页。');
  assert.equal(snap.rides.at(-1).impact_count, 2);
  assert.deepEqual(snap.last_review && [snap.last_review.text, snap.last_review.sequence], ['比上次少 10 秒', 21]);
  assert.equal(feed.snapshot(snap.last_id).events.length, 0);
});

test('USB plug and unplug show up once each', () => {
  const feed = createFeed();
  feed.onWatchLine('[19:00:17] == port error: GetOverlappedResult failed');
  feed.onWatchLine('[19:00:18] == port error: could not open port');
  feed.onWatchLine('[19:00:22] == opened COM5');
  const snap = feed.snapshot();
  assert.deepEqual(snap.events.map((e) => [e.kind, e.connected]), [['link', false], ['link', true]]);
  assert.equal(snap.watch_connected, true);
});

test('a failed ride_latest call does not become a ride', () => {
  const feed = createFeed();
  feed.recordToolCall('ride_latest', {}, { isError: true, content: [{ text: '{"ok":false,"error":"手表无回复"}' }] });
  feed.recordToolCall('ride_latest', {}, { content: [{ text: '{"ok":true,"sequence":29,"elapsed_s":20,"moving_s":10,"still_s":9,"pause_count":0,"impact_count":2}' }] });
  feed.recordToolCall('review_write', {}, { content: [{ text: '{"ok":true,"text":"复盘","bytes":6}' }] });
  const snap = feed.snapshot();
  assert.deepEqual(snap.rides.map((r) => r.sequence), [29]);
  assert.equal(snap.last_review.sequence, 29);
});
