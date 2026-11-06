// Agent dashboard: polls /api/agent/feed and animates what the ai_agent in
// the emulator decided and what the real watch executed.
const $ = (id) => document.getElementById(id);
const WHO = {
  heartbeat: '心跳', idle: '心跳', step: 'Agent', tool: '调用手表',
  watch: '手表', user: '你', agent: 'Agent', proactive: '主动', link: '接入',
};
let since = 0;
let lastReviewAt = 0;
let lastFollowUp = '';

const fmtTime = (t) => new Date(t).toTimeString().slice(0, 8);
function fmtSec(s) {
  if (s < 60) return `${s}秒`;
  const m = Math.floor(s / 60);
  return s % 60 ? `${m}分${s % 60}秒` : `${m}分钟`;
}

function flash(id, cls = 'flash', ms = 900) {
  const el = $(id);
  if (!el) return;
  el.classList.remove(cls);
  void el.offsetWidth;
  el.classList.add(cls);
  setTimeout(() => el.classList.remove(cls), ms);
}
const pulse = (id, delay = 0) => setTimeout(() => flash(id, 'go', 750), delay);

function animate(e) {
  if (e.kind === 'heartbeat') { flash('n-agent'); $('beat').textContent = `心跳 · ${fmtTime(e.t)}`; $('beat').classList.add('live'); }
  if (e.kind === 'step' || e.kind === 'agent' || e.kind === 'proactive') { pulse('l-llm'); flash('n-llm'); flash('n-agent'); }
  if (e.kind === 'tool') {
    flash('n-agent'); pulse('l-mcp'); setTimeout(() => flash('n-gw'), 350);
    pulse('l-usb', 400); setTimeout(() => flash('n-watch'), 800);
    $('watch-act').textContent = e.text;
  }
  if (e.kind === 'watch' || e.kind === 'link') { flash('n-watch'); $('watch-act').textContent = e.text; }
  if (e.kind === 'link' && e.connected) pulse('l-usb');
}

function addEvent(e) {
  const li = document.createElement('li');
  li.className = `k-${e.kind}`;
  const time = document.createElement('time');
  time.textContent = fmtTime(e.t);
  const who = document.createElement('span');
  who.className = 'who';
  who.textContent = WHO[e.kind] || e.kind;
  const what = document.createElement('span');
  what.className = 'what';
  what.textContent = e.kind === 'tool' ? `${e.text}  ${e.tool}${e.ok === false ? '  ✗ 失败' : ''}` : e.text;
  li.append(time, who, what);
  const list = $('events');
  list.append(li);
  while (list.children.length > 120) list.firstChild.remove();
  list.scrollTop = list.scrollHeight;
  if ((e.kind === 'proactive' || e.kind === 'agent') && /冲击/.test(e.text)) lastFollowUp = e.text;
}

function delta(label, cur, prev, { unit = (v) => v, warnUp = false } = {}) {
  let cls = 'same';
  let text = '与上次持平';
  if (prev === undefined) text = '首条记录';
  else if (cur !== prev) {
    const d = cur - prev;
    cls = d > 0 ? (warnUp ? 'warn' : 'up') : 'down';
    text = `比上次 ${d > 0 ? '+' : '−'}${unit(Math.abs(d))}`;
  }
  return `<div class="delta"><small>${label}</small><b>${unit(cur)}</b><span class="${cls}">${text}</span></div>`;
}

function renderCompare(rides) {
  $('count').textContent = `骑行记录 · ${rides.length} 条`;
  const cur = rides[rides.length - 1];
  const prev = rides[rides.length - 2];
  if (!cur) { $('deltas').innerHTML = ''; $('chart').innerHTML = ''; return; }
  $('deltas').innerHTML =
    delta(`运动时长 · #${cur.sequence}`, cur.moving_s, prev?.moving_s, { unit: fmtSec }) +
    delta('冲击次数', cur.impact_count, prev?.impact_count, { unit: (v) => `${v}次`, warnUp: true }) +
    delta('暂停次数', cur.pause_count, prev?.pause_count, { unit: (v) => `${v}次` });

  const shown = rides.slice(-6);
  const W = 560, H = 250, top = 34, bottom = 30, left = 10;
  const max = Math.max(60, ...shown.map((r) => r.elapsed_s));
  const slot = (W - left * 2) / shown.length;
  const bw = Math.min(56, slot * 0.5);
  let svg = `<line x1="${left}" y1="${H - bottom}" x2="${W - left}" y2="${H - bottom}" stroke="#1d252b"/>`;
  shown.forEach((r, i) => {
    const x = left + slot * i + (slot - bw) / 2;
    const scale = (v) => (v / max) * (H - top - bottom);
    const hm = scale(r.moving_s), hs = scale(Math.max(0, r.elapsed_s - r.moving_s));
    const isCur = r === cur;
    const y0 = H - bottom;
    svg += `<rect x="${x}" y="${y0 - hm}" width="${bw}" height="${Math.max(hm, 1)}" rx="4" fill="#b7ff22" opacity="${isCur ? 1 : 0.38}"/>`;
    svg += `<rect x="${x}" y="${y0 - hm - hs}" width="${bw}" height="${hs}" rx="4" fill="#34414a" opacity="${isCur ? 1 : 0.6}"/>`;
    svg += `<text x="${x + bw / 2}" y="${y0 - hm - hs - 8}" text-anchor="middle" class="val${isCur ? ' cur' : ''}">${fmtSec(r.elapsed_s)}</text>`;
    for (let k = 0; k < Math.min(r.impact_count, 8); k++) {
      svg += `<circle cx="${x + bw + 8}" cy="${y0 - 6 - k * 11}" r="4" fill="#ffb020"/>`;
    }
    svg += `<text x="${x + bw / 2}" y="${H - 10}" text-anchor="middle" class="${isCur ? 'cur' : ''}">#${r.sequence}${isCur ? ' 本次' : ''}</text>`;
  });
  $('chart').innerHTML = svg;

  if (cur.impact_count > 0) {
    $('safety').hidden = false;
    $('safety-text').textContent = lastFollowUp || `记录 #${cur.sequence} 有 ${cur.impact_count} 次冲击，等待 Agent 跟进…`;
  } else $('safety').hidden = true;
}

function renderReview(review) {
  if (!review || review.t === lastReviewAt) return;
  lastReviewAt = review.t;
  $('review-seq').textContent = review.sequence ? `复盘 · 记录 #${review.sequence}` : '复盘';
  $('review-text').textContent = review.text;
  document.querySelector('.face').classList.add('flash');
  setTimeout(() => document.querySelector('.face').classList.remove('flash'), 1600);
}

async function tick() {
  try {
    const res = await fetch(`/api/agent/feed?since=${since}`, { cache: 'no-store' });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error);
    data.events.forEach((e, i) => setTimeout(() => { addEvent(e); animate(e); }, i * 120));
    since = data.last_id;
    renderCompare(data.rides);
    renderReview(data.last_review);
    const on = data.watch_connected;
    $('link').textContent = on ? '手表 · 已接入' : '手表 · 未接入（骑行中）';
    $('link').classList.toggle('live', on);
    $('link').classList.toggle('off', !on);
    $('n-watch').classList.toggle('offline', !on);
  } catch (error) {
    $('beat').textContent = `网关未连接：${error.message}`;
    $('beat').classList.remove('live');
  }
}

$('reply').addEventListener('submit', async (event) => {
  event.preventDefault();
  const input = $('reply-text');
  const text = input.value.trim();
  if (!text) return;
  const button = event.target.querySelector('button');
  button.disabled = true;
  try {
    const res = await fetch('/api/agent/ask', {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ text }),
    });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error);
    input.value = '';
  } catch (error) {
    input.placeholder = `发送失败：${error.message}`;
  } finally { button.disabled = false; }
});

tick();
setInterval(tick, 700);
