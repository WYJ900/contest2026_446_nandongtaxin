import { validateRide } from './ride-data.js';
import { RideBleTransport } from './ble-transport.js';

const UUID = {
  service: '0000fff0-0000-1000-8000-00805f9b34fb',
  tx: '0000fff1-0000-1000-8000-00805f9b34fb',
  rx: '0000fff2-0000-1000-8000-00805f9b34fb',
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const $ = (id) => document.getElementById(id);

const state = {
  device: null,
  tx: null,
  rx: null,
  serialPort: null,
  serialReader: null,
  serialWriter: null,
  serialReadTask: null,
  serialReadyResolve: null,
  transport: null,
  syncInFlight: false,
  demo: false,
  receiveBuffer: '',
  watchReview: '',
  recordSequence: null,
  recordExtras: {},
  reviewSequence: null,
  reviewRequest: 0,
  reviewController: null,
  ride: { elapsed_s: 0, moving_s: 0, still_s: 0, impacts: 0, active: false },
};

const controls = ['syncTimeButton', 'statusButton', 'latestButton'];
const bleClient = new RideBleTransport({
  onMessage: handleMessage,
  onDisconnect: () => {
    state.rx = null;
    state.tx = null;
    state.transport = null;
    $('connectButton').textContent = '连接手表（BLE）';
    $('sendReviewButton').disabled = true;
    setConnected(false, '手表连接已断开');
    log('BLE 已断开');
  },
});

// 2026-09-18 验证记录中的历史读数，仅供显式加载样例。
const DEMO_RIDE = {
  elapsed_s: 8,
  moving_s: 5,
  still_s: 1,
  pause_count: 1,
  impacts: 0,
  distance_km: 0,
};

function log(message, data) {
  const stamp = new Date().toLocaleTimeString('zh-CN', { hour12: false });
  $('log').textContent += `[${stamp}] ${message}${data ? ` ${JSON.stringify(data)}` : ''}\n`;
  $('log').scrollTop = $('log').scrollHeight;
}

function setConnected(connected, label = connected ? '已连接' : '未连接') {
  $('connectionBadge').textContent = label;
  $('connectionBadge').className = `badge ${connected ? 'online' : 'offline'}`;
  controls.forEach((id) => { $(id).disabled = !connected; });
}

// AI 配置与设备连接分开显示；配置存在不代表云端请求一定成功。
function setGatewayStatus(ai, provider = 'AI', model = '') {
  const badge = $('aiBadge');
  if (ai && ai !== 'fallback') {
    badge.textContent = `${provider} 已配置${model ? ` · ${model}` : ''}`;
    badge.className = 'badge online';
  } else {
    badge.textContent = '本地降级';
    badge.className = 'badge offline';
  }
}

function updateClock() {
  const now = new Date();
  $('clock').textContent = now.toLocaleTimeString('zh-CN', {
    hour: '2-digit', minute: '2-digit', hour12: false,
  });
}

function renderRide(ride) {
  state.ride = { ...state.ride, ...ride };
  $('rideState').textContent = ride.active ? (ride.paused ? '已暂停' : '骑行中') : '已结束/待开始';
  $('elapsedValue').textContent = `${ride.elapsed_s || 0} 秒`;
  $('movingValue').textContent = `${ride.moving_s || 0} 秒`;
  $('impactValue').textContent = `${ride.impacts || 0} 次`;
}

const rideInputs = {
  elapsed_s: 'elapsedInput', moving_s: 'movingInput', still_s: 'stillInput',
  distance_km: 'distanceInput', pause_count: 'pauseInput', impacts: 'impactInput',
};

function readRideInputs() {
  return { ...state.recordExtras, ...Object.fromEntries(Object.entries(rideInputs).map(([field, id]) =>
    [field, $(id).validity.badInput ? NaN : Number($(id).value || 0)])) };
}

function invalidateReview(message = '') {
  state.reviewRequest++;
  state.reviewController?.abort();
  state.reviewController = null;
  state.watchReview = '';
  state.reviewSequence = null;
  $('reviewText').value = '';
  $('aiSource').textContent = message;
  $('generateButton').disabled = false;
  $('sendReviewButton').disabled = true;
}

// 历史样例与本次手工录入分别标记。
function prefillRide(ride = DEMO_RIDE) {
  $('elapsedInput').value = ride.elapsed_s;
  $('movingInput').value = ride.moving_s;
  $('stillInput').value = ride.still_s;
  $('distanceInput').value = ride.distance_km;
  $('pauseInput').value = ride.pause_count;
  $('impactInput').value = ride.impacts;
  renderRide({ ...ride, active: false, paused: false });
  $('rideState').textContent = '历史样例';
  log('已加载 2026-09-18 历史样例，请勿当作本次骑行记录', ride);
}

function handleMessage(message) {
  log('手表返回', message);
  if (message.event === 'ride_status') renderRide(message);
  if (message.event === 'ride_latest') {
    invalidateReview('已读取手表保存的骑行记录，可以生成复盘。');
    state.recordSequence = message.sequence;
    state.recordExtras = Object.fromEntries(Object.entries(message).filter(([key]) =>
      key.startsWith('gps_') || key.startsWith('safety_') || key === 'rest_count'));
    for (const [field, id] of Object.entries(rideInputs)) {
      $(id).value = message[field] ?? 0;
    }
    if (message.gps_time_ms > 0) $('distanceInput').value = message.gps_distance_m / 1000;
    renderRide(message);
    $('rideState').textContent = `手表记录 #${message.sequence}`;
  }
  if (message.event === 'ride_review' && message.ok) {
    $('aiSource').textContent += ' · 手表已确认保存';
  }
  if (message.event === 'time_sync') {
    $('timeText').textContent = new Date(message.epoch * 1000).toLocaleString('zh-CN');
  }
}

function parseSerialLine(rawLine) {
  const line = rawLine.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '').trim();
  if (!line || line.startsWith('nsh>') || line.startsWith('vela>')) return;

  const jsonStart = line.indexOf('{');
  const jsonEnd = line.lastIndexOf('}');
  if (jsonStart >= 0 && jsonEnd > jsonStart) {
    try {
      const message = JSON.parse(line.slice(jsonStart, jsonEnd + 1));
      if (message.active !== undefined && !message.event) message.event = 'ride_status';
      handleMessage(message);
      return;
    } catch {}
  }

  if (line.includes('VELARIDE_TIME: set epoch=')) {
    const match = line.match(/epoch=(\d+)/);
    if (match) handleMessage({ ok: line.includes('ret=0'), event: 'time_sync', epoch: Number(match[1]) });
  } else if (line.includes('VELARIDE_REVIEW:') || line.includes('VELARIDE_PERSIST:')) {
    log('手表状态', line);
  }
}

function signalSerialReady(text) {
  if (state.serialReadyResolve && text.includes('nsh>')) {
    const resolve = state.serialReadyResolve;
    state.serialReadyResolve = null;
    resolve();
  }
}

async function serialReadLoop(port) {
  const reader = port.readable.getReader();
  state.serialReader = reader;
  let pending = '';
  try {
    while (state.serialPort === port) {
      const { value, done } = await reader.read();
      if (done) break;
      pending += decoder.decode(value, { stream: true });
      signalSerialReady(pending);
      let newline;
      while ((newline = pending.search(/[\r\n]/)) >= 0) {
        const line = pending.slice(0, newline);
        pending = pending.slice(newline + 1).replace(/^[\r\n]+/, '');
        parseSerialLine(line);
      }
    }
  } catch (error) {
    log('USB 串口读取结束', { error: error.message });
  } finally {
    if (state.serialReader === reader) state.serialReader = null;
    reader.releaseLock();
  }
}

function waitForSerialReady(timeoutMs = 15000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      if (state.serialReadyResolve) state.serialReadyResolve = null;
      reject(new Error('等待手表 nsh> 启动提示超时'));
    }, timeoutMs);
    state.serialReadyResolve = () => {
      clearTimeout(timer);
      resolve();
    };
  });
}

async function disconnectSerial() {
  const port = state.serialPort;
  if (!port) return;
  state.serialPort = null;
  state.transport = null;
  state.serialReadyResolve = null;
  if (state.serialReader) {
    try { await state.serialReader.cancel(); } catch {}
  }
  if (state.serialWriter) {
    try { state.serialWriter.releaseLock(); } catch {}
    state.serialWriter = null;
  }
  try { await port.close(); } catch {}
  $('serialButton').textContent = '通过 USB 串口连接';
  setConnected(false, 'USB 已断开');
  log('USB 串口已释放');
}

async function bridgeRequest(action, payload = {}) {
  const response = await fetch('/api/device', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action, ...payload }),
  });
  const data = await response.json();
  if (!response.ok || !data.ok) throw new Error(data.error || '设备桥接失败');
  const lines = String(data.output || '').split(/[\r\n]+/);
  lines.forEach(parseSerialLine);
  return data.output || '';
}

async function connectBridge() {
  if (state.transport === 'bridge') {
    state.transport = null;
    $('serialButton').textContent = '启用 USB 短连接网关';
    setConnected(false, 'USB 网关已停用');
    log('USB 短连接网关已停用；COM5 未被占用');
    return;
  }

  $('serialButton').disabled = true;
  try {
    state.demo = false;
    state.transport = 'bridge';
    setConnected(true, 'USB 短连接网关');
    $('serialButton').textContent = '停用 USB 短连接网关';
    log('USB 网关已启用；每次操作执行后立即释放 COM5');
    await syncTime();
    await writeFrame({ cmd: 'ride_status' });
  } catch (error) {
    state.transport = null;
    setConnected(false, 'USB 网关失败');
    $('serialButton').textContent = '启用 USB 短连接网关';
    throw error;
  } finally {
    $('serialButton').disabled = false;
  }
}

async function connectSerial() {
  if (state.serialPort) {
    await disconnectSerial();
    return;
  }

  if (!navigator.serial) throw new Error('当前浏览器不支持 Web Serial，请使用桌面 Chrome/Edge');
  $('serialButton').disabled = true;
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: 1_000_000, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
  state.serialPort = port;
  state.serialWriter = port.writable.getWriter();
  state.transport = 'serial';
  state.demo = false;
  state.serialReadTask = serialReadLoop(port);
  const ready = waitForSerialReady();
  if (typeof port.setSignals === 'function') {
    /* Match tools/reset.ps1 exactly. Opening CH340 may leave RTS in an
     * indeterminate state; make one deliberate full reset before use. */
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    await port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise((resolve) => setTimeout(resolve, 300));
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
  }
  setConnected(true, 'USB 串口已连接');
  $('serialButton').textContent = '断开 USB 串口';
  $('serialButton').disabled = false;
  log('USB 串口连接成功，波特率 1000000');
  log('已执行完整 RTS 复位，等待 nsh>');
  await ready;
  log('手表启动完成');
  await syncTime();
  await writeFrame({ cmd: 'ride_status' });
}

async function serialCommand(command) {
  if (!state.serialWriter) throw new Error('USB 串口未连接');
  await state.serialWriter.write(encoder.encode(`${command}\r\n`));
}

function bytesToHex(text) {
  return [...encoder.encode(text)].map((byte) => byte.toString(16).padStart(2, '0')).join('');
}

async function writeSerialFrame(payload) {
  let command;
  switch (payload.cmd) {
    case 'ping': command = 'date'; break;
    case 'time_sync': command = `velaride time-set ${Math.trunc(payload.epoch)}`; break;
    case 'phone_data': command = `velaride phone-data ${Number(payload.speed).toFixed(1)} ${Number(payload.distance).toFixed(2)}`; break;
    case 'ride_status': command = 'velaride ride-status'; break;
    case 'ride_review': {
      const hex = bytesToHex(payload.text);
      await serialCommand('velaride review-begin');
      for (let offset = 0; offset < hex.length; offset += 32) {
        await serialCommand(`velaride review-part ${hex.slice(offset, offset + 32)}`);
        await new Promise((resolve) => setTimeout(resolve, 35));
      }
      await serialCommand('velaride review-end');
      log('USB 已发送', { cmd: payload.cmd, parts: Math.ceil(hex.length / 32) });
      return;
    }
    default: throw new Error(`USB 串口不支持命令 ${payload.cmd}`);
  }
  await serialCommand(command);
  log('USB 已发送', { cmd: payload.cmd });
}

async function writeFrame(payload) {
  if (state.demo) {
    log('演示发送', payload);
    if (payload.cmd === 'ride_status') handleMessage({ ok: true, event: 'ride_status', ...state.ride });
    if (payload.cmd === 'time_sync') handleMessage({ ok: true, event: 'time_sync', epoch: payload.epoch });
    return;
  }

  if (state.transport === 'serial') {
    await writeSerialFrame(payload);
    return;
  }

  if (state.transport === 'bridge') {
    switch (payload.cmd) {
      case 'time_sync':
        await bridgeRequest('time_sync', { epoch: payload.epoch });
        break;
      case 'phone_data':
        await bridgeRequest('phone_data', {
          speed: payload.speed,
          distance: payload.distance,
        });
        break;
      case 'ride_status':
        await bridgeRequest('ride_status');
        break;
      case 'ride_review':
        await bridgeRequest('review_upload', { review: payload.text });
        break;
      case 'ping':
        await bridgeRequest('ride_status');
        break;
      default:
        throw new Error(`USB 网关不支持命令 ${payload.cmd}`);
    }
    log('USB 短连接操作完成，COM5 已释放', { cmd: payload.cmd });
    return;
  }

  const response = await bleClient.request(payload);
  log('手表已确认', { command: payload.cmd });
  return response;
}

async function connect() {
  if (bleClient.connected) {
    bleClient.disconnect();
    return;
  }
  if (!navigator.bluetooth) throw new Error('当前浏览器不支持 Web Bluetooth，请使用电脑 Chrome/Edge');
  $('connectButton').disabled = true;
  try {
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'VelaRide' }],
    optionalServices: [UUID.service],
  });
  await bleClient.connect(device);
  state.tx = bleClient.tx;
  state.rx = bleClient.rx;
  state.device = device;
  state.transport = 'ble';
  state.demo = false;
  setConnected(true, device.name || 'VelaRide');
  $('connectButton').textContent = '断开手表';
  $('sendReviewButton').disabled = !state.watchReview;
  log('BLE 连接成功');
  await writeFrame({ cmd: 'ping' });
  await syncTime();
  } catch (error) {
    bleClient.disconnect();
    throw error;
  } finally {
    $('connectButton').disabled = false;
  }
}

async function syncTime() {
  if (state.syncInFlight) {
    log('校时正在进行，忽略重复点击');
    return;
  }
  state.syncInFlight = true;
  $('syncTimeButton').disabled = true;
  const epoch = Math.floor(Date.now() / 1000);
  try {
    await writeFrame({ cmd: 'time_sync', epoch, timezone_min: -new Date().getTimezoneOffset() });
    $('timeText').textContent = new Date().toLocaleString('zh-CN');
  } finally {
    state.syncInFlight = false;
    $('syncTimeButton').disabled = !(state.demo || state.rx || state.serialWriter || state.transport === 'bridge');
  }
}

async function generateReview() {
  if (state.reviewController) return;
  invalidateReview();
  const requestId = state.reviewRequest;
  const recordSequence = state.recordSequence;
  const controller = new AbortController();
  state.reviewController = controller;
  const timeout = setTimeout(() => controller.abort(), 65_000);
  $('generateButton').disabled = true;
  $('aiSource').textContent = 'AI 正在分析骑行记录，请稍候…';
  try {
    const payload = validateRide(readRideInputs());

    const response = await fetch('/api/ride/review', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(payload),
      signal: controller.signal,
    });
    const data = await response.json();
    if (!response.ok || !data.ok) throw new Error(data.error || '生成失败');
    if (requestId !== state.reviewRequest) return;
    $('reviewText').value = data.review;
    state.watchReview = data.watch_review || data.review;
    state.reviewSequence = recordSequence;
    const reasons = {
      not_configured: '未配置 AI 凭据',
      configuration_error: 'AI 接口配置不完整或有误',
      timeout: 'AI 请求超时',
      empty_response: 'AI 服务未返回复盘正文',
      output_limit: 'AI 输出达到限制，未返回复盘正文',
      upstream_error: `AI 服务暂不可用${data.upstream_status ? `（HTTP ${data.upstream_status}）` : ''}`,
    };
    $('aiSource').textContent = data.source !== 'fallback'
      ? `由 ${data.provider || 'AI'} 生成${data.model ? ` · ${data.model}` : ''}`
      : `${reasons[data.fallback_reason] || 'AI 暂不可用'}，使用网关本地规则降级`;
    if (data.watch_source === 'font-safe') {
      $('aiSource').textContent += ' · 手表将使用字库兼容的规则短版';
    }
    $('sendReviewButton').disabled = !bleClient.connected;
    log('复盘已生成', { source: data.source });
  } catch (error) {
    if (requestId !== state.reviewRequest) return;
    const message = error.name === 'AbortError' ? '请求超时，请稍后重试' : error.message;
    $('aiSource').textContent = `生成失败：${message}`;
    log('复盘失败', { error: message });
  } finally {
    clearTimeout(timeout);
    if (requestId === state.reviewRequest) {
      state.reviewController = null;
      $('generateButton').disabled = false;
    }
  }
}

$('connectButton').addEventListener('click', () => connect().catch((error) => log('连接失败', { error: error.message })));
$('serialButton').addEventListener('click', () => connectBridge().catch((error) => log('USB 网关失败', { error: error.message })));
$('demoButton').addEventListener('click', () => {
  bleClient.disconnect();
  state.recordSequence = null;
  state.recordExtras = {};
  state.demo = true;
  state.transport = 'demo';
  setConnected(false, '手表未连接 · 手工录入');
  $('syncTimeButton').disabled = true;
  $('sendDataButton').disabled = true;
  $('statusButton').disabled = true;
  renderRide({ ...readRideInputs(), active: false, paused: false });
  $('rideState').textContent = '手工录入';
  invalidateReview();
  $('elapsedInput').focus();
  log('请照本次手表结束页填写；已有输入已保留');
});
$('sampleButton').addEventListener('click', () => {
  bleClient.disconnect();
  state.recordSequence = null;
  state.recordExtras = {};
  invalidateReview('已加载历史样例，生成的复盘仅对应这条样例。');
  prefillRide();
});
Object.values(rideInputs).forEach((id) => {
  $(id).addEventListener('input', () => {
    state.recordSequence = null;
    state.recordExtras = {};
    invalidateReview('记录已修改，请重新生成复盘。');
    renderRide({ ...readRideInputs(), active: false, paused: false });
    $('rideState').textContent = '手工录入';
  });
});
$('syncTimeButton').addEventListener('click', () => syncTime().catch((error) => log('校时失败', { error: error.message })));
$('speedInput').addEventListener('input', () => {
  $('speedOutput').textContent = `${Number($('speedInput').value).toFixed(1)} km/h`;
  $('speedValue').textContent = Number($('speedInput').value).toFixed(1);
});
$('sendDataButton').addEventListener('click', () => writeFrame({
  cmd: 'phone_data',
  speed: Number($('speedInput').value),
  distance: Number($('distanceInput').value),
}).catch((error) => log('数据发送失败', { error: error.message })));
$('statusButton').addEventListener('click', () => writeFrame({ cmd: 'ride_status' }).catch((error) => log('读取失败', { error: error.message })));
$('latestButton').addEventListener('click', () => writeFrame({ cmd: 'ride_latest' }).catch((error) => {
  $('aiSource').textContent = `读取失败：${error.message}`;
  log('读取失败', { error: error.message });
}));
$('generateButton').addEventListener('click', () => generateReview().catch((error) => log('复盘失败', { error: error.message })));
$('sendReviewButton').addEventListener('click', async () => {
  $('sendReviewButton').disabled = true;
  try {
    await writeFrame({ cmd: 'ride_review', text: state.watchReview,
      ...(state.reviewSequence !== null ? { sequence: state.reviewSequence } : {}) });
  } catch (error) {
    $('aiSource').textContent = `回传失败：${error.message}`;
    log('复盘回传失败', { error: error.message });
  } finally {
    $('sendReviewButton').disabled = !bleClient.connected || !state.watchReview;
  }
});

$('supportHint').textContent = '连接手表后读取最近骑行记录，生成复盘并回传。AI 不可用时会明确显示本地规则降级。';
setConnected(false, '手表未连接');
setGatewayStatus(null);
updateClock();
setInterval(updateClock, 1000);
fetch('/api/health', { signal: AbortSignal.timeout(5000) }).then((res) => {
  if (!res.ok) throw new Error('健康检查失败');
  return res.json();
}).then((health) => {
  setGatewayStatus(health.ai, health.provider, health.model);
  if (health.config_error) log('AI 配置错误', { error: health.config_error });
  log('网关服务就绪', health);
}).catch((error) => {
  setGatewayStatus(null);
  $('aiBadge').textContent = '网关不可用';
  log('网关服务异常', { error: error.message });
});
