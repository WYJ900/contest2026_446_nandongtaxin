// Minimal MCP (Streamable HTTP, JSON responses) server exposing the real
// watch to the openvela ai_agent: the agent in the emulator decides, the
// watch executes. The watch is reached through serial_logger.py: commands
// are appended to its command file and replies read back from its log, so
// the logger keeps sole ownership of COM5 and every exchange stays recorded.
import { appendFile, open, stat } from 'node:fs/promises';
import { clipUtf8 } from './ai-provider.mjs';
import { WATCH_FONT_CHARACTERS } from './watch-font.mjs';

const PROTOCOL = '2025-03-26';
const watchFont = new Set(WATCH_FONT_CHARACTERS);
const ANSI = /\x1b\[[0-9;]*[A-Za-z]/g;

const TOOLS = [
  {
    name: 'ride_status',
    description: '读取手表当前骑行状态：active 是否在骑行、paused 是否暂停、elapsed_s 总时长秒、moving_s 运动秒、still_s 静止秒、impact_count 冲击次数、rest_count 已提醒休息次数。',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'ride_latest',
    description: '读取手表最近一次已结束骑行的记录（sequence 编号、时长、运动/静止、暂停、冲击、休息提醒）。',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'ride_pause',
    description: '在手表上暂停当前骑行。只能在用户明确同意休息后调用。',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'ride_resume',
    description: '在手表上继续当前骑行。只能在用户明确同意继续后调用。',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'watch_chime',
    description: '让手表响一阵提醒音（闹钟音连响 4 次，最长 10 秒，自动停止），用于主动提醒骑行者休息。',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'review_write',
    description: '把一段中文骑行复盘写入手表最近一次骑行，显示在手表复盘页。最多约 80 个汉字，只写真实记录里有的数据。',
    inputSchema: {
      type: 'object',
      properties: { text: { type: 'string', description: '复盘正文' } },
      required: ['text'],
    },
  },
];

export function watchSafeText(text) {
  const kept = [...String(text)].filter((ch) => {
    const code = ch.codePointAt(0);
    return (code >= 0x20 && code < 0x7f) || watchFont.has(ch);
  }).join('').trim();
  return clipUtf8(kept, 260);
}

export function createWatchLink({ cmdFile, logFile, timeoutMs = 6000, pollMs = 150 }) {
  let chain = Promise.resolve();

  async function size() {
    try { return (await stat(logFile)).size; } catch { return 0; }
  }
  async function readFrom(offset) {
    const handle = await open(logFile, 'r');
    try {
      const { size: end } = await handle.stat();
      if (end <= offset) return '';
      const buffer = Buffer.alloc(end - offset);
      await handle.read(buffer, 0, buffer.length, offset);
      return buffer.toString('utf8');
    } finally { await handle.close(); }
  }

  // Sends one shell line and returns the first log line after its echo that
  // satisfies `match`. Exchanges are serialized so replies cannot interleave.
  function exchange(command, match) {
    const run = async () => {
      const start = await size();
      await appendFile(cmdFile, command + '\n', 'utf8');
      const deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, pollMs));
        const text = (await readFrom(start)).replace(ANSI, '');
        const echo = text.indexOf('>> ' + command);
        if (echo < 0) continue;
        for (const raw of text.slice(echo).split('\n').slice(1)) {
          const line = raw.replace(/^\[\d\d:\d\d:\d\d\]\s*/, '').trim();
          const found = match(line);
          if (found !== undefined) return found;
        }
      }
      throw new Error(`手表无回复：${command}`);
    };
    const result = chain.then(run, run);
    chain = result.catch(() => {});
    return result;
  }

  const jsonLine = (line) => {
    if (!line.startsWith('{') || !line.endsWith('}')) return undefined;
    try { return JSON.parse(line); } catch { return undefined; }
  };
  const reviewLine = (prefix) => (line) => {
    const at = line.indexOf('VELARIDE_REVIEW: ');
    if (at < 0) return undefined;
    const body = line.slice(at + 17);
    return body.startsWith(prefix) || /decode=|empty|failed/.test(body) ? body : undefined;
  };

  return {
    rideStatus: () => exchange('velaride ride-status', jsonLine),
    rideLatest: () => exchange('velaride ride-latest', jsonLine),
    ridePause: () => exchange('velaride ride-pause', jsonLine),
    rideResume: () => exchange('velaride ride-resume', jsonLine),
    chime: () => exchange('velaride sound play alarm', (line) => {
      const at = line.indexOf('VELARIDE_SOUND_STATUS: ');
      return at < 0 ? undefined : line.slice(at + 23);
    }),
    async writeReview(text) {
      const safe = watchSafeText(text);
      if (!safe) throw new Error('复盘为空或全部是手表字库外字符');
      const bytes = Buffer.from(safe, 'utf8');
      const begin = await exchange('velaride review-begin', reviewLine('transfer begin'));
      if (!begin.startsWith('transfer begin')) throw new Error(begin);
      for (let i = 0; i < bytes.length; i += 32) {
        const hex = bytes.subarray(i, i + 32).toString('hex');
        const part = await exchange(`velaride review-part ${hex}`, reviewLine('part='));
        if (!part.startsWith('part=')) throw new Error(part);
      }
      const end = await exchange('velaride review-end', reviewLine('transfer bytes='));
      if (!/persist=0/.test(end)) throw new Error(end);
      return { ok: true, text: safe, bytes: bytes.length };
    },
  };
}

async function callTool(link, name, args) {
  switch (name) {
    case 'ride_status': return link.rideStatus();
    case 'ride_latest': return link.rideLatest();
    case 'ride_pause': return link.ridePause();
    case 'ride_resume': return link.rideResume();
    case 'watch_chime': {
      // ret=0 means the cue was accepted; it plays right after, so the status
      // line still shows active=0 pending=1. Say so explicitly for the model.
      const status = await link.chime();
      const ok = /ret=0\b/.test(status);
      return { ok, result: ok ? '手表已响提示音' : '手表提示音失败', status };
    }
    case 'review_write': return link.writeReview(args?.text ?? '');
    default: throw Object.assign(new Error(`unknown tool ${name}`), { rpc: -32602 });
  }
}

// Handles one JSON-RPC message; returns the response object, or null for notifications.
export async function handleMcp(link, message, log = () => {}) {
  const { id, method, params } = message || {};
  const reply = (result) => ({ jsonrpc: '2.0', id, result });
  const fail = (code, text) => ({ jsonrpc: '2.0', id, error: { code, message: text } });
  if (id === undefined || id === null) return null;
  if (method === 'initialize') {
    return reply({ protocolVersion: PROTOCOL, capabilities: { tools: {} }, serverInfo: { name: 'velaride-watch', version: '0.1.0' } });
  }
  if (method === 'ping') return reply({});
  if (method === 'tools/list') return reply({ tools: TOOLS });
  if (method === 'tools/call') {
    const name = params?.name;
    try {
      const result = await callTool(link, name, params?.arguments);
      log(`tool ${name} ok ${JSON.stringify(result).slice(0, 200)}`);
      return reply({ content: [{ type: 'text', text: JSON.stringify(result) }] });
    } catch (error) {
      if (error.rpc) return fail(error.rpc, error.message);
      log(`tool ${name} failed: ${error.message}`);
      return reply({ content: [{ type: 'text', text: JSON.stringify({ ok: false, error: error.message }) }], isError: true });
    }
  }
  return fail(-32601, `method not found: ${method}`);
}
