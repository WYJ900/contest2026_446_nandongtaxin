import { createServer } from 'node:http';
import { execFile } from 'node:child_process';
import { readFile, stat } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';

const root = fileURLToPath(new URL('./public/', import.meta.url));
const projectRoot = fileURLToPath(new URL('../', import.meta.url));
const execFileAsync = promisify(execFile);
const port = Number(process.env.PORT || 4173);
const maxBody = 32 * 1024;
const watchCjk = new Set([...('安败保本标测成持冲出传次存到地动度放分否复感公后户击即记继间检建将较结进静久就距开离里连练录率没秒目内盘配骑器请区全确认入设失时始事是室束速随态提停退外完未我息下效心行醒休需绪续选训要一已议用由有于运暂择长止置中钟状自总组分钟占比明显稳偏可路段节奏贯注意补水车辆身体和平定')]);

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

function normalizeRide(input = {}) {
  const number = (value) => Number.isFinite(Number(value)) ? Number(value) : 0;
  return {
    elapsed_s: Math.max(0, number(input.elapsed_s)),
    moving_s: Math.max(0, number(input.moving_s)),
    still_s: Math.max(0, number(input.still_s)),
    impacts: Math.max(0, number(input.impacts)),
    pause_count: Math.max(0, number(input.pause_count)),
    distance_km: Math.max(0, number(input.distance_km)),
  };
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
  const ride = normalizeRide(input);
  const minutes = Math.max(1, Math.round(ride.elapsed_s / 60));
  const movingRatio = ride.elapsed_s > 0
    ? Math.round((ride.moving_s / ride.elapsed_s) * 100)
    : 0;
  const safety = ride.impacts > 0
    ? `检测到 ${ride.impacts} 次冲击，建议检查车辆和身体状态。`
    : '本次没有记录到明显冲击，安全状态平稳。';
  const rest = ride.still_s > ride.moving_s
    ? '静止时间偏长，下次可在安全路段保持更连续的节奏。'
    : '运动节奏较连贯，继续注意补水和定时休息。';

  return `骑行 ${minutes} 分钟，运动占比 ${movingRatio}%。${safety}${rest}`;
}

function watchCanRender(text) {
  for (const char of text) {
    const code = char.codePointAt(0);
    if (code >= 0x3400 && code <= 0x9fff && !watchCjk.has(char)) return false;
  }
  return true;
}

async function callMimo(ride) {
  const anthropicToken = process.env.ANTHROPIC_AUTH_TOKEN;
  const anthropicBase = (process.env.ANTHROPIC_BASE_URL || '').replace(/\/$/, '');
  const apiKey = process.env.MIMO_API_KEY;
  const baseUrl = (process.env.MIMO_BASE_URL ||
    'https://api.xiaomimimo.com/v1').replace(/\/$/, '');
  const model = process.env.MIMO_MODEL || 'mimo-v2-flash';

  if (!apiKey && !anthropicToken) return null;

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 60_000);
  try {
    const system = `你是骑行安全助手。根据记录生成不超过70字的中文复盘，先总结，再给一条安全建议。不要编造心率或路线。为了手表字体兼容，汉字只能使用这些字符：${[...watchCjk].join('')}`;
    const response = anthropicToken
      ? await fetch(`${anthropicBase}/v1/messages`, {
          method: 'POST',
          headers: {
            authorization: `Bearer ${anthropicToken}`,
            'anthropic-version': '2023-06-01',
            'content-type': 'application/json',
          },
          body: JSON.stringify({
            model: process.env.ANTHROPIC_MODEL || 'mimo-v2.5',
            max_tokens: 2048,
            temperature: 0.3,
            thinking: { type: 'disabled' },
            system,
            messages: [{ role: 'user', content: JSON.stringify(ride) }],
          }),
          signal: controller.signal,
        })
      : await fetch(`${baseUrl}/chat/completions`, {
      method: 'POST',
      headers: {
        authorization: `Bearer ${apiKey}`,
        'content-type': 'application/json',
      },
      body: JSON.stringify({
        model,
        temperature: 0.3,
        max_tokens: 2048,
        messages: [
          {
            role: 'system',
            content: system,
          },
          { role: 'user', content: JSON.stringify(ride) },
        ],
      }),
      signal: controller.signal,
    });

    if (!response.ok) {
      throw new Error(`MiMo HTTP ${response.status}`);
    }

    const data = await response.json();
    const text = anthropicToken
      ? (data?.content?.find((part) => part.type === 'text')?.text ||
         data?.choices?.[0]?.message?.content ||
         data?.completion || data?.output_text)?.trim()
      : data?.choices?.[0]?.message?.content?.trim();
    if (!text) {
      const keys = data && typeof data === 'object' ? Object.keys(data).join(',') : typeof data;
      const blocks = Array.isArray(data?.content)
        ? data.content.map((part) => `${part?.type || '?'}[${Object.keys(part || {}).join('|')}]`).join(',')
        : 'not-array';
      throw new Error(`MiMo 返回内容为空（字段：${keys || 'none'}；内容块：${blocks}）`);
    }
    const clipped = text.slice(0, 380);
    return {
      review: clipped,
      watch_safe: watchCanRender(clipped),
    };
  } finally {
    clearTimeout(timeout);
  }
}

async function handleApi(req, res, url) {
  if (req.method === 'GET' && url.pathname === '/api/health') {
    json(res, 200, {
      ok: true,
      ai: process.env.ANTHROPIC_AUTH_TOKEN
        ? 'mimo-anthropic'
        : process.env.MIMO_API_KEY ? 'mimo-openai' : 'fallback',
    });
    return true;
  }

  if (req.method === 'POST' && url.pathname === '/api/ride/review') {
    try {
      const ride = normalizeRide(await readJson(req));
      let aiResult;
      let review;
      let watchReview;
      let watchSource;
      let source = process.env.ANTHROPIC_AUTH_TOKEN
        ? 'mimo-anthropic'
        : 'mimo-openai';

      try {
        aiResult = await callMimo(ride);
      } catch (error) {
        console.error('[review] MiMo 调用失败，使用本地降级：', error.message);
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
        watch_review: watchReview,
        watch_source: watchSource,
        ride,
      });
    } catch (error) {
      json(res, 400, { ok: false, error: error.message });
    }
    return true;
  }

  if (req.method === 'POST' && url.pathname === '/api/device') {
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
    console.log(`AI 模式：${process.env.ANTHROPIC_AUTH_TOKEN || process.env.MIMO_API_KEY ? 'Xiaomi MiMo' : '本地降级'}`);
  });
}
