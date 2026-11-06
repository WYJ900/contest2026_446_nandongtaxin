// Live feed for the Agent dashboard (public/agent.html). It only observes:
// MCP tool calls are recorded by server.mjs, and three log files are tailed
// read-only — the ai_agent console log, the emulator syslog (heartbeat and
// tool trace) and the watch serial log (what the real watch actually did).
import { open, stat } from 'node:fs/promises';

const ANSI = /\x1b\[[0-9;]*[A-Za-z]/g;
const PLACEHOLDER = /^(正在|稍等|马上|让我|思考|处理中|好的，?我来)/;
const MAX_EVENTS = 400;

export const TOOL_LABELS = {
  vr__ride_status: '读取骑行状态',
  vr__ride_latest: '读取最近骑行记录',
  vr__review_write: '复盘写回手表',
  vr__watch_chime: '手表响铃提醒',
  vr__ride_pause: '暂停骑行',
  vr__ride_resume: '继续骑行',
  read_file: '读 Skill / 记忆',
  write_file: '写入记忆',
  edit_file: '写入记忆',
  get_current_time: '读当前时间',
};

// Ride records keyed by sequence; only real watch output is accepted.
export function parseRideRecord(line) {
  const at = line.indexOf('{"ok":true,"sequence":');
  if (at < 0) return null;
  const end = line.indexOf('}', at);
  if (end < 0) return null;
  try {
    const r = JSON.parse(line.slice(at, end + 1));
    return Number.isInteger(r.sequence) ? r : null;
  } catch { return null; }
}

export function createFeed({ agentLog, emuLog, watchLog, historyLogs = [] } = {}) {
  const events = [];
  const rides = new Map();
  let nextId = 1;
  let lastReview = null;
  let replyOpen = false;
  let watchConnected = null;

  function push(kind, text, extra = {}) {
    const event = { id: nextId++, t: Date.now(), kind, text, ...extra };
    events.push(event);
    if (events.length > MAX_EVENTS) events.shift();
    return event;
  }

  function addRide(record) {
    // Failed tool calls return {ok:false,error}; only real records have a sequence.
    if (!record || !Number.isInteger(record.sequence)) return;
    rides.set(record.sequence, record);
  }

  function onAgentLine(raw) {
    const line = raw.replace(/^\[\d\d:\d\d:\d\d\] /, '');
    if (line.startsWith('>> ask ')) { push('user', line.slice(7)); replyOpen = false; return; }
    if (line.startsWith('[Agent]: ')) {
      const text = line.slice(9).trim();
      replyOpen = !PLACEHOLDER.test(text);
      if (replyOpen) push('agent', text);
      return;
    }
    if (line.startsWith('vela>')) { replyOpen = false; return; }
    if (replyOpen && line.trim()) {
      const last = events[events.length - 1];
      if (last?.kind === 'agent') last.text += '\n' + line.trim();
    }
  }

  function onEmuLine(line) {
    if (line.includes('[heartbeat] Triggered agent check')) { push('heartbeat', '定时心跳：Agent 主动检查'); return; }
    let m = line.match(/iter=\d+ tool=(\S+)/);
    if (m && m[1] !== '(none)' && !m[1].startsWith('vr__')) {
      push('step', TOOL_LABELS[m[1]] || m[1], { tool: m[1] });
      return;
    }
    m = line.match(/System message \[heartbeat\]: (.*)/);
    if (m) {
      const text = m[1].trim();
      if (text === 'HEARTBEAT_OK') push('idle', '没有新记录，不打扰');
      else if (text && !PLACEHOLDER.test(text)) push('proactive', text);
    }
  }

  // serial_logger.py marks USB plug/unplug with "== opened" / "== port error".
  function linkState(line) {
    if (line.includes('== opened ')) return true;
    if (line.includes('== port error')) return false;
    return null;
  }

  function onWatchLine(line) {
    const link = linkState(line);
    if (link !== null) {
      if (link !== watchConnected) push('link', link ? '手表已接入（USB 充电线）' : '手表已断开', { connected: link });
      watchConnected = link;
      return;
    }
    addRide(parseRideRecord(line));
    if (line.includes('VELARIDE_RIDE: start')) push('watch', '开始骑行');
    else if (line.includes('VELARIDE_RIDE: pause')) push('watch', '已暂停');
    else if (line.includes('VELARIDE_RIDE: resume')) push('watch', '已继续');
    else if (line.includes('VELARIDE_RIDE: finish')) {
      const m = line.match(/seq=(\d+) duration=(\d+)/);
      push('watch', m ? `骑行结束 · 记录 #${m[1]} · ${m[2]} 秒` : '骑行结束');
    } else if (/VELARIDE_SOUND: cue=6 result=0/.test(line)) push('watch', '响铃');
    else if (line.includes('VELARIDE_REVIEW: transfer bytes=')) push('watch', '复盘已写入复盘页');
  }

  // Called by server.mjs for every MCP tools/call the agent makes.
  function recordToolCall(name, args, result) {
    const tool = `vr__${name}`;
    let data = null;
    try { data = JSON.parse(result?.content?.[0]?.text ?? 'null'); } catch { /* keep null */ }
    if (name === 'ride_latest') addRide(data);
    if (name === 'review_write' && data?.ok) {
      const seq = Math.max(0, ...rides.keys());
      lastReview = { text: data.text, sequence: seq || null, t: Date.now() };
    }
    push('tool', TOOL_LABELS[tool] || tool, { tool, ok: !result?.isError && data?.ok !== false });
  }

  const tails = [];
  function tail(path, handler, fromStart) {
    if (!path) return;
    tails.push({ path, handler, pos: fromStart ? 0 : -1, rest: '' });
  }
  tail(agentLog, onAgentLine, false);
  tail(emuLog, onEmuLine, false);
  tail(watchLog, onWatchLine, false);

  async function readNew(t) {
    let size;
    try { size = (await stat(t.path)).size; } catch { return; }
    if (t.pos < 0) t.pos = size;             // start at the end: live events only
    if (size < t.pos) { t.pos = 0; t.rest = ''; } // file recreated
    if (size === t.pos) return;
    const handle = await open(t.path, 'r');
    try {
      const buffer = Buffer.alloc(size - t.pos);
      await handle.read(buffer, 0, buffer.length, t.pos);
      t.pos = size;
      const lines = (t.rest + buffer.toString('utf8')).split('\n');
      t.rest = lines.pop();
      for (const line of lines) t.handler(line.replace(ANSI, '').replace(/\r$/, ''));
    } finally { await handle.close(); }
  }

  async function poll() {
    for (const t of tails) {
      try { await readNew(t); } catch { /* log rotated or locked; retry next tick */ }
    }
  }

  // Past ride records already printed by the watch (ride-latest replies).
  async function loadHistory() {
    for (const path of [watchLog, ...historyLogs].filter(Boolean)) {
      try {
        const handle = await open(path, 'r');
        try {
          const text = (await handle.readFile()).toString('utf8');
          for (const line of text.split('\n')) {
            addRide(parseRideRecord(line));
            if (path === watchLog && linkState(line) !== null) watchConnected = linkState(line);
          }
        } finally { await handle.close(); }
      } catch { /* missing history file is fine */ }
    }
  }

  let timer = null;
  return {
    recordToolCall,
    onAgentLine, onEmuLine, onWatchLine,
    async start(intervalMs = 400) {
      await loadHistory();
      await poll();
      timer = setInterval(poll, intervalMs);
      timer.unref?.();
    },
    stop() { if (timer) clearInterval(timer); },
    snapshot(since = 0) {
      return {
        events: events.filter((e) => e.id > since),
        rides: [...rides.values()].sort((a, b) => a.sequence - b.sequence),
        last_review: lastReview,
        watch_connected: watchConnected !== false,
        last_id: nextId - 1,
      };
    },
  };
}
