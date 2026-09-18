import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const appPath = fileURLToPath(new URL('../public/app.js', import.meta.url));
const source = readFileSync(appPath, 'utf8');

test('录入按钮预填的读数与手表结束页一致', () => {
  // 数值来自演示视频中手表“本次骑行记录”页的真实读数，不得放大或编造。
  const expected = { elapsed_s: 8, moving_s: 5, still_s: 1, pause_count: 1, impacts: 0 };
  for (const [key, value] of Object.entries(expected)) {
    assert.match(
      source,
      new RegExp(`${key}:\\s*${value}\\b`),
      `DEMO_RIDE.${key} 应为 ${value}`,
    );
  }
});

test('录入按钮走预填流程而不是清零', () => {
  assert.match(source, /function prefillRide\(/);
  assert.match(source, /prefillRide\(\);/);
  assert.doesNotMatch(
    source,
    /renderRide\(\{\s*active: false,\s*elapsed_s: 0/,
    '录入按钮不应再把表单清零',
  );
});

test('预填后仍要求总时长大于零', () => {
  assert.match(source, /请先填写手表显示的真实总时长/);
});
