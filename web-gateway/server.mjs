import { createServer } from 'node:http';
import { execFile } from 'node:child_process';
import { readFile, stat } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { validateRide } from './public/ride-data.js';
import { resolveConfig, publicConfig, requestReview, clipUtf8 } from './ai-provider.mjs';
import { WATCH_FONT_CHARACTERS } from './watch-font.mjs';

const root = fileURLToPath(new URL('./public/', import.meta.url));
const projectRoot = fileURLToPath(new URL('../', import.meta.url));
const execFileAsync = promisify(execFile);
const port = Number(process.env.PORT || 4173);
const maxBody = 32 * 1024;
const watchCjk = new Set(WATCH_FONT_CHARACTERS);

const mimeTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
};

function json(res, status, body) {
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
  });
  res.end(JSON.stringify(body));
}

async function readJson(req) {
  const chunks = [];
  let size = 0;

  for await (const chunk of req) {
    size += chunk.length;
    if (size > maxBody) throw new Error('请求体过大');
    chunks.push(chunk);
  }

  return JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
}

async function runDeviceBridge(input = {}) {
  const allowed = new Set(['time_sync', 'ride_status', 'phone_data', 'review_upload']);
  const action = String(input.action || '');
  if (!allowed.has(action)) throw new Error('不支持的设备操作');

  const args = [
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', join(projectRoot, 'tools', 'web_device_bridge.ps1'),
    '-Action', action,
    '-Port', 'COM5',
  ];
  if (action === 'time_sync') args.push('-Epoch', String(Math.trunc(Number(input.epoch))));
  if (action === 'phone_data') {
    args.push('-Speed', String(Number(input.speed || 0)));
    args.push('-Distance', String(Number(input.distance || 0)));
  }
  if (action === 'review_upload') {
    const review = String(input.review || '');
    if (!review || Buffer.byteLength(review, 'utf8') > 260) throw new Error('复盘为空或过长');
    args.push('-ReviewBase64', Buffer.from(review, 'utf8').toString('base64'));
  }

  const { stdout } = await execFileAsync('powershell.exe', args, {
    cwd: projectRoot,
    encoding: 'utf8',
    timeout: action === 'review_upload' ? 20_000 : 8_000,
    windowsHide: true,
    maxBuffer: 128 * 1024,
  });
  return stdout;
}

export function fallbackReview(input) {
  const ride = validateRide(input);
  const minutes = Math.floor(ride.elapsed_s / 60);
  const seconds = ride.elapsed_s % 60;
  const duration = minutes > 0
    ? `${minutes} 分钟${seconds ? ` ${seconds} 秒` : ''}`
    : `${seconds} 秒`;
  const movingRatio = ride.elapsed_s > 0
    ? Math.round((ride.moving_s / ride.elapsed_s) * 100)
    : 0;
  const safety = ride.impacts > 0
    ? `检测到 ${ride.impacts} 次冲击，建议检查车辆和身体状态。`
    : '本次没有记录到明显冲击。';
  const rest = ride.still_s > ride.moving_s
    ? '静止时间偏长，下次可在安全路段保持更连续的节奏。'
    : '运动节奏较连贯，继续注意补水和定时休息。';
  const response = ride.safety_version === 1
    ? `安全确认 ${ride.safety_checks} 次，确认无事 ${ride.safety_confirmed} 次，暂停 ${ride.safety_pauses} 次。`
      + (ride.safety_help > 0 ? `求助意向 ${ride.safety_help} 次，不表示已发送消息。` : '')
      + (ride.safety_unanswered > 0 ? `${ride.safety_unanswered} 次未及时确认，可能之后已处理。` : '') : '';
  const choices = ride.extras_version === 1 ? `分段 ${ride.lap_count} 次，选择休息 ${ride.rest_taken} 次，确认补水 ${ride.drink_done} 次。` : '';
  return `骑行 ${duration}，运动占比 ${movingRatio}%。${safety}${response}${choices}${rest}`;
}

function watchCanRender(text) {
  for (const char of text) {
    const code = char.codePointAt(0);
    if ((code >= 0x20 && code <= 0x7e) || char === '\n' || char === '\t') continue;
    if (!watchCjk.has(char)) return false;
  }
  return true;
}

async function handleApi(req, res, url) {
  if (req.method === 'GET' && url.pathname === '/api/health') {
    try {
      const config = resolveConfig();
      json(res, 200, { ok: true, ai: config.key ? config.source : 'fallback',
        ...publicConfig(config) });
    } catch (error) {
      json(res, 200, { ok: true, ai: 'fallback', configured: false,
        config_error: error.message });
    }
    return true;
  }

  if (req.method === 'POST' && url.pathname === '/api/ride/review') {
    try {
      const ride = validateRide(await readJson(req));
      let aiResult;
      let review;
      let watchReview;
      let watchSource;
      let fallbackReason = null;
      let source = 'fallback';
      let config;
      let upstreamStatus = null;
      let originalReview = null;
      try {
        config = resolveConfig();
      } catch {
        fallbackReason = 'configuration_error';
      }
      if (config) {
        source = config.source;
        try {
          const deadline = Date.now() + config.timeoutMs;
          const system = '根据骑行记录，用常用中文写一段不超过70字的复盘：先概括，再给一条可执行建议。只输出正文。只能引用实际字段；不编造心率、路线、天气、受伤或安全结论。没有 gps_time_ms 时零距离表示未记录；gps_avg_centi 是有效采样均速，单位0.01 km/h，不是全程均速。冲击不等于摔车。safety_help 只代表求助意向，不能说已通知联系人；safety_unanswered 是未及时确认次数，可与之后确认重叠。旧记录无处理字段不代表已确认安全。';
          const text = await requestReview(config, ride, system);
          if (text) aiResult = { review: [...text].slice(0, 380).join(''), watch_safe: watchCanRender(text) };
          else fallbackReason = 'not_configured';
          if (aiResult && !aiResult.watch_safe && deadline - Date.now() > 1000) {
            const unsupported = [...new Set([...text].filter((char) => !watchCanRender(char)))];
            try {
              const adapted = await requestReview({ ...config, timeoutMs: deadline - Date.now() },
                { ...ride, draft_review: aiResult.review, unsupported_characters: unsupported },
                '将 draft_review 改写成不超过70字的常用中文，保留全部事实和建议，避开 unsupported_characters 中的字，用常用近义表达替换。只输出改写后的复盘，不解释。');
              if (adapted && watchCanRender(adapted)) {
                originalReview = aiResult.review;
                aiResult = { review: [...adapted].slice(0, 380).join(''), watch_safe: true };
              }
            } catch {
              // The first AI result remains valid; explicit font fallback follows.
            }
          }
        } catch (error) {
          fallbackReason = error.name === 'AbortError' ? 'timeout' :
            ['empty_response', 'output_limit'].includes(error.code) ? error.code : 'upstream_error';
          upstreamStatus = error.status || null;
          console.error('[review] AI request failed; using local fallback:', fallbackReason, upstreamStatus || '');
        }
      }

      if (aiResult) {
        review = aiResult.review;
        if (aiResult.watch_safe) {
          watchReview = review;
          watchSource = source;
        } else {
          watchReview = fallbackReview(ride);
          watchSource = 'font-safe';
        }
      } else {
        review = fallbackReview(ride);
        watchReview = review;
        watchSource = 'fallback';
        source = 'fallback';
      }

      json(res, 200, {
        ok: true,
        source,
        review,
        watch_review: clipUtf8(watchReview),
        watch_source: watchSource,
        ride,
        fallback_reason: fallbackReason,
        adapted_for_watch: originalReview !== null,
        ...(originalReview ? { original_review: originalReview } : {}),
        upstream_status: upstreamStatus,
        ...(config ? publicConfig(config) : {}),
      });
    } catch (error) {
      json(res, 400, { ok: false, error: error.message });
    }
    return true;
  }

  if (req.method === 'POST' && url.pathname === '/api/device') {
    if (process.env.VELARIDE_ENABLE_DEVICE_BRIDGE !== '1') {
      json(res, 403, { ok: false, error: '当前模式已停用 USB 设备接口' });
      return true;
    }
    try {
      const input = await readJson(req);
      const output = await runDeviceBridge(input);
      json(res, 200, { ok: true, output });
    } catch (error) {
      json(res, 400, { ok: false, error: error.message });
    }
    return true;
  }

  return false;
}

async function serveStatic(req, res, url) {
  const requested = url.pathname === '/' ? '/index.html' : url.pathname;
  const relative = normalize(decodeURIComponent(requested)).replace(/^([/\\])+/, '');
  const path = join(root, relative);

  if (!path.startsWith(root)) {
    res.writeHead(403).end('Forbidden');
    return;
  }

  try {
    const info = await stat(path);
    if (!info.isFile()) throw new Error('not a file');
    const content = await readFile(path);
    res.writeHead(200, {
      'content-type': mimeTypes[extname(path)] || 'application/octet-stream',
      'cache-control': 'no-cache',
    });
    res.end(content);
  } catch {
    res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
    res.end('未找到页面');
  }
}

export function createAppServer() {
  return createServer(async (req, res) => {
    const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);
    if (await handleApi(req, res, url)) return;
    if (req.method === 'GET' || req.method === 'HEAD') {
      await serveStatic(req, res, url);
      return;
    }
    json(res, 404, { ok: false, error: 'not_found' });
  });
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  createAppServer().listen(port, '127.0.0.1', () => {
    console.log(`VelaRide Web 网关：http://127.0.0.1:${port}`);
    console.log('AI 配置状态请查看 /api/health（不会返回 Key）');
  });
}
