// 用本机 Chrome 的无头模式驱动 VelaRide Web 网关，验证「生成复盘」拿到的是真实
// MiMo 原文，而不是本地降级文本。输出 1600x1000 的 PNG 供人工核对与录屏参考。
//
//   node scripts/capture-demo.mjs [--out <dir>] [--url http://127.0.0.1:4173]
//
// 依赖：本机已安装 Chrome；服务端已用 scripts/start-gateway.ps1 启动。

import { spawn } from 'node:child_process';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const CHROME_CANDIDATES = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
  join(process.env.LOCALAPPDATA || '', 'Google/Chrome/Application/chrome.exe'),
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
];

const argOf = (name, fallback) => {
  const index = process.argv.indexOf(name);
  return index === -1 ? fallback : process.argv[index + 1];
};

const url = argOf('--url', 'http://127.0.0.1:4173');
const outDir = argOf('--out', 'submission-preview');
const viewport = { width: 1600, height: 1000 };

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function findBrowser() {
  const found = CHROME_CANDIDATES.find((candidate) => candidate && existsSync(candidate));
  if (!found) throw new Error('未找到 Chrome 或 Edge，请安装后重试');
  return found;
}

async function launchBrowser() {
  const profile = await mkdtemp(join(tmpdir(), 'velaride-capture-'));
  const child = spawn(findBrowser(), [
    '--headless=new',
    '--remote-debugging-port=0',
    `--user-data-dir=${profile}`,
    `--window-size=${viewport.width},${viewport.height}`,
    '--hide-scrollbars',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-extensions',
    'about:blank',
  ], { stdio: ['ignore', 'pipe', 'pipe'] });

  const target = await new Promise((resolve, reject) => {
    let buffer = '';
    const timer = setTimeout(() => reject(new Error('等待调试端口超时')), 20_000);
    child.stderr.on('data', (chunk) => {
      buffer += chunk.toString();
      const match = buffer.match(/ws:\/\/127\.0\.0\.1:(\d+)\/devtools\/browser\//);
      if (match) {
        clearTimeout(timer);
        resolve({ profile, port: Number(match[1]) });
      }
    });
    child.on('exit', (code) => {
      clearTimeout(timer);
      reject(new Error(`浏览器提前退出，退出码 ${code}`));
    });
  });

  return { child, ...target };
}

async function openPage(port) {
  const response = await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: 'PUT' });
  return response.json();
}

function connect(wsUrl) {
  const socket = new WebSocket(wsUrl);
  let nextId = 1;
  const pending = new Map();

  socket.addEventListener('message', (event) => {
    const message = JSON.parse(event.data);
    const entry = pending.get(message.id);
    if (!entry) return;
    pending.delete(message.id);
    if (message.error) entry.reject(new Error(message.error.message));
    else entry.resolve(message.result);
  });

  const ready = new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, { once: true });
    socket.addEventListener('error', reject, { once: true });
  });

  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });

  return { ready, send, close: () => socket.close() };
}

async function evaluate(session, expression) {
  const result = await session.send('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
  });
  if (result.exceptionDetails) {
    throw new Error(result.exceptionDetails.exception?.description || '页面执行失败');
  }
  return result.result.value;
}

async function waitFor(session, expression, { timeout = 90_000, interval = 400 } = {}) {
  const deadline = Date.now() + timeout;
  for (;;) {
    const value = await evaluate(session, expression);
    if (value) return value;
    if (Date.now() > deadline) throw new Error(`等待超时：${expression}`);
    await sleep(interval);
  }
}

async function main() {
  await mkdir(outDir, { recursive: true });
  const browser = await launchBrowser();
  const page = await openPage(browser.port);
  const session = connect(page.webSocketDebuggerUrl);

  try {
    await session.ready;
    await session.send('Page.enable');
    await session.send('Runtime.enable');
    await session.send('Emulation.setDeviceMetricsOverride', {
      ...viewport, deviceScaleFactor: 2, mobile: false,
    });
    await session.send('Page.navigate', { url });
    await waitFor(session, 'document.readyState === "complete"');

    const health = await evaluate(session, 'fetch("/api/health").then((r) => r.json())');
    if (health.ai === 'fallback') {
      throw new Error('服务端报告 ai=fallback，未配置 MiMo 凭据，拒绝出图');
    }

    await evaluate(session, 'document.getElementById("demoButton").click(); true');
    await sleep(600);
    const prefilled = await evaluate(session, 'JSON.stringify({'
      + 'elapsed: document.getElementById("elapsedInput").value,'
      + 'moving: document.getElementById("movingInput").value,'
      + 'still: document.getElementById("stillInput").value,'
      + 'pause: document.getElementById("pauseInput").value,'
      + 'impact: document.getElementById("impactInput").value})');

    await evaluate(session, 'document.getElementById("generateButton").click(); true');
    const source = await waitFor(session, `(() => {
      const text = document.getElementById('aiSource').textContent;
      return (text.includes('由 Xiaomi MiMo 生成') || text.includes('降级')) ? text : '';
    })()`);

    if (!source.includes('由 Xiaomi MiMo 生成')) {
      throw new Error(`未拿到真实 MiMo 结果，页面显示：${source}`);
    }

    const review = await evaluate(session, 'document.getElementById("reviewText").value');
    await evaluate(session, 'document.querySelector(".review-card").scrollIntoView({block: "center"}); true');
    await sleep(500);
    const shot = await session.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
    const file = join(outDir, 'web-mimo-review.png');
    await writeFile(file, Buffer.from(shot.data, 'base64'));

    console.log(`service     : ${health.ai}`);
    console.log(`prefilled   : ${prefilled}`);
    console.log(`source label: ${source}`);
    console.log(`review      : ${review}`);
    console.log(`screenshot  : ${file}`);
  } finally {
    session.close();
    browser.child.kill();
    await rm(browser.profile, { recursive: true, force: true }).catch(() => {});
  }
}

main().catch((error) => {
  console.error(`失败：${error.message}`);
  process.exit(1);
});
