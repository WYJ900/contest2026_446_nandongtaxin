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
  ride: { elapsed_s: 0, moving_s: 0, still_s: 0, impacts: 0, active: false },
};

const controls = ['syncTimeButton', 'sendDataButton', 'statusButton'];

// 手表结束页“本次骑行记录”的真实读数。演示时按此填入，不做任何放大或编造。
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

// 徽标在没有手表连接时反映的是网关 AI 通道，而不是手表连接状态。
// 稳定演示不连手表，若沿用“未连接”会被误读成 AI 不可用。
function setGatewayStatus(ai) {
  const badge = $('connectionBadge');
  if (ai && ai !== 'fallback') {
    badge.textContent = 'MiMo 已就绪';
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

// 将手表结束页真实读数填入表单与预览，等价于用户逐项手填。
function prefillRide(ride = DEMO_RIDE) {
  $('elapsedInput').value = ride.elapsed_s;
  $('movingInput').value = ride.moving_s;
  $('stillInput').value = ride.still_s;
  $('distanceInput').value = ride.distance_km;
  $('pauseInput').value = ride.pause_count;
  $('impactInput').value = ride.impacts;
  renderRide({ ...ride, active: false, paused: false });
  log('已按手表结束页填入真实读数', ride);
}

function handleMessage(message) {
  log('手表返回', message);
  if (message.event === 'ride_status') renderRide(message);
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

function onNotification(event) {
  state.receiveBuffer += decoder.decode(event.target.value);
  let newline;
  while ((newline = state.receiveBuffer.indexOf('\n')) >= 0) {
    const frame = state.receiveBuffer.slice(0, newline).trim();
    state.receiveBuffer = state.receiveBuffer.slice(newline + 1);
    if (!frame) continue;
    try { handleMessage(JSON.parse(frame)); }
    catch { log('无法解析手表消息', frame); }
  }
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

  if (!state.rx) throw new Error('手表未连接');
  const bytes = encoder.encode(`${JSON.stringify(payload)}\n`);
  for (let offset = 0; offset < bytes.length; offset += 18) {
    const chunk = bytes.slice(offset, offset + 18);
    if (typeof state.rx.writeValueWithoutResponse === 'function') {
      await state.rx.writeValueWithoutResponse(chunk);
    } else {
      await state.rx.writeValue(chunk);
    }
    await new Promise((resolve) => setTimeout(resolve, 8));
  }
  log('已发送', payload);
}

async function connect() {
  if (!navigator.bluetooth) throw new Error('当前浏览器不支持 Web Bluetooth，请使用电脑 Chrome/Edge');
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'VelaRide' }],
    optionalServices: [UUID.service],
  });
  device.addEventListener('gattserverdisconnected', () => {
    state.tx = null;
    state.rx = null;
    setConnected(false, '连接断开');
    log('BLE 已断开');
  });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(UUID.service);
  state.tx = await service.getCharacteristic(UUID.tx);
  state.rx = await service.getCharacteristic(UUID.rx);
  await state.tx.startNotifications();
  state.tx.addEventListener('characteristicvaluechanged', onNotification);
  state.device = device;
  state.transport = 'ble';
  state.demo = false;
  setConnected(true, device.name || 'VelaRide');
  log('BLE 连接成功');
  await writeFrame({ cmd: 'ping' });
  await syncTime();
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
  $('generateButton').disabled = true;
  $('aiSource').textContent = 'AI 正在分析骑行记录，请稍候…';
  try {
    const payload = {
      ...state.ride,
      elapsed_s: Number($('elapsedInput').value || 0),
      moving_s: Number($('movingInput').value || 0),
      still_s: Number($('stillInput').value || 0),
      pause_count: Number($('pauseInput').value || 0),
      impacts: Number($('impactInput').value || 0),
      distance_km: Number($('distanceInput').value || 0),
    };
    if (!Number.isFinite(payload.elapsed_s) || payload.elapsed_s <= 0) {
      throw new Error('请先填写手表显示的真实总时长');
    }
    if (payload.moving_s + payload.still_s > payload.elapsed_s + 2) {
      throw new Error('运动时长与静止时长之和不能大于总时长');
    }

    const response = await fetch('/api/ride/review', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const data = await response.json();
    if (!response.ok || !data.ok) throw new Error(data.error || '生成失败');
    $('reviewText').value = data.review;
    state.watchReview = data.watch_review || data.review;
    $('aiSource').textContent = data.source.startsWith('mimo') ? '由 Xiaomi MiMo 生成' : '当前无 MiMo Key，使用端侧规则降级';
    $('sendReviewButton').disabled = true;
    log('复盘已生成', { source: data.source });
  } finally {
    $('generateButton').disabled = false;
  }
}

$('connectButton').addEventListener('click', () => connect().catch((error) => log('连接失败', { error: error.message })));
$('serialButton').addEventListener('click', () => connectBridge().catch((error) => log('USB 网关失败', { error: error.message })));
$('demoButton').addEventListener('click', () => {
  state.demo = true;
  state.transport = 'demo';
  setConnected(true, '真实结果录入');
  $('syncTimeButton').disabled = true;
  $('sendDataButton').disabled = true;
  $('statusButton').disabled = true;
  prefillRide();
  $('sendReviewButton').disabled = true;
  $('reviewText').value = '';
  $('aiSource').textContent = '';
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
$('generateButton').addEventListener('click', () => generateReview().catch((error) => log('复盘失败', { error: error.message })));
$('sendReviewButton').addEventListener('click', () => writeFrame({ cmd: 'ride_review', text: state.watchReview || $('reviewText').value.trim() }).catch((error) => log('复盘回传失败', { error: error.message })));

$('supportHint').textContent = '手表独立完成骑行；网页只调用真实 MiMo。为防止黑屏，当前版本绝不打开 COM5。';
setConnected(false);
setGatewayStatus(null);
updateClock();
setInterval(updateClock, 1000);
fetch('/api/health').then((res) => res.json()).then((health) => {
  setGatewayStatus(health.ai);
  log('网关服务就绪', health);
}).catch((error) => {
  setGatewayStatus(null);
  log('网关服务异常', { error: error.message });
});
